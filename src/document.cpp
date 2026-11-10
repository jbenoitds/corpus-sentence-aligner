// SPDX-License-Identifier: MIT
#include "csa/document.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "csa/utf8.hpp"
#include "csa/zip.hpp"

namespace csa {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw DocumentError("cannot open file: " + path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string lower_extension(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

void push_paragraph(std::vector<std::string>& out, const std::string& raw) {
    std::string p = utf8::normalize_segment(raw);
    if (!p.empty()) out.push_back(std::move(p));
}

// Tag name without namespace-agnostic tricks: WordprocessingML always uses
// the "w:" prefix in practice, and markup compatibility uses "mc:".
struct Tag {
    std::string name;
    std::string attrs;
    bool closing = false;
    bool self_closing = false;
};

Tag parse_tag(const std::string& xml, std::size_t lt, std::size_t gt) {
    Tag t;
    std::size_t i = lt + 1;
    if (i < gt && xml[i] == '/') {
        t.closing = true;
        ++i;
    }
    std::size_t end = gt;
    if (end > i && xml[end - 1] == '/') {
        t.self_closing = true;
        --end;
    }
    std::size_t name_end = i;
    while (name_end < end && !std::isspace(static_cast<unsigned char>(xml[name_end]))) ++name_end;
    t.name = xml.substr(i, name_end - i);
    t.attrs = xml.substr(name_end, end - name_end);
    return t;
}

std::string attr_value(const std::string& attrs, const std::string& key) {
    std::size_t p = 0;
    while ((p = attrs.find(key, p)) != std::string::npos) {
        bool boundary = (p == 0 || std::isspace(static_cast<unsigned char>(attrs[p - 1])));
        std::size_t q = p + key.size();
        while (q < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[q]))) ++q;
        if (boundary && q < attrs.size() && attrs[q] == '=') {
            ++q;
            while (q < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[q]))) ++q;
            if (q < attrs.size() && (attrs[q] == '"' || attrs[q] == '\'')) {
                char quote = attrs[q];
                std::size_t r = attrs.find(quote, q + 1);
                if (r != std::string::npos) return attrs.substr(q + 1, r - q - 1);
            }
        }
        p += key.size();
    }
    return "";
}

bool looks_like_citation_number(const std::string& s) {
    bool any_digit = false;
    for (unsigned char c : s) {
        if (c >= '0' && c <= '9') {
            any_digit = true;
        } else if (c == ',' || c == '-' || c == ' ' || c == ';') {
            continue;
        } else if (c >= 0x80) {
            continue;  // en dash, no-break space... (UTF-8 bytes)
        } else {
            return false;
        }
    }
    if (!any_digit) return false;
    // Be conservative: reject any non-ASCII that is not a dash or space.
    std::u32string u = utf8::decode(s);
    for (char32_t c : u) {
        if (c >= 0x80 && c != 0x2013 && c != 0x2014 && c != 0x2011 && c != 0xA0 && c != 0x202F) return false;
    }
    return true;
}

}  // namespace

std::string xml_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        std::size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back('&');
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else if (!ent.empty() && ent[0] == '#') {
            unsigned long cp = 0;
            char* endp = nullptr;
            if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) cp = std::strtoul(ent.c_str() + 2, &endp, 16);
            else cp = std::strtoul(ent.c_str() + 1, &endp, 10);
            if (endp && *endp == '\0' && cp > 0 && cp <= 0x10FFFF) utf8::append(out, static_cast<char32_t>(cp));
            else out += "&" + ent + ";";
        } else {
            out += "&" + ent + ";";
        }
        i = semi;
    }
    return out;
}

std::vector<std::string> docx_paragraphs_from_xml(const std::string& xml, const LoadOptions& opts) {
    std::vector<std::string> out;
    // Paragraphs can nest (text boxes inside a paragraph), so keep a stack.
    std::vector<std::string> para_stack;
    std::string run_text;
    bool in_run = false;
    bool run_superscript = false;
    int in_text = 0;       // inside <w:t>
    int skip_depth = 0;    // inside deleted text, moved-from text, mc:Fallback, field instructions
    int rpr_depth = 0;     // inside <w:rPr> of the current run
    int tabs_depth = 0;    // inside <w:tabs> (tab-stop definitions)

    auto current = [&]() -> std::string* {
        if (para_stack.empty()) return nullptr;
        return &para_stack.back();
    };
    auto flush_run = [&]() {
        if (!run_text.empty()) {
            bool drop = run_superscript && !opts.keep_superscript_numbers && looks_like_citation_number(run_text);
            if (!drop) {
                if (std::string* p = current()) *p += run_text;
            }
        }
        run_text.clear();
    };
    auto emit_text = [&](const std::string& t) {
        if (skip_depth > 0) return;
        if (in_run) run_text += t;
        else if (std::string* p = current()) *p += t;
    };

    std::size_t i = 0;
    const std::size_t n = xml.size();
    while (i < n) {
        if (xml[i] != '<') {
            std::size_t lt = xml.find('<', i);
            if (lt == std::string::npos) lt = n;
            if (in_text > 0) emit_text(xml_unescape(xml.substr(i, lt - i)));
            i = lt;
            continue;
        }
        // Comments, processing instructions, CDATA.
        if (xml.compare(i, 4, "<!--") == 0) {
            std::size_t e = xml.find("-->", i + 4);
            i = (e == std::string::npos) ? n : e + 3;
            continue;
        }
        if (xml.compare(i, 9, "<![CDATA[") == 0) {
            std::size_t e = xml.find("]]>", i + 9);
            if (e == std::string::npos) e = n;
            if (in_text > 0) emit_text(xml.substr(i + 9, e - i - 9));
            i = (e == n) ? n : e + 3;
            continue;
        }
        if (xml.compare(i, 2, "<?") == 0 || xml.compare(i, 2, "<!") == 0) {
            std::size_t e = xml.find('>', i);
            i = (e == std::string::npos) ? n : e + 1;
            continue;
        }
        std::size_t gt = xml.find('>', i);
        if (gt == std::string::npos) break;
        Tag tag = parse_tag(xml, i, gt);
        i = gt + 1;
        const std::string& nm = tag.name;

        bool skipper = (nm == "w:del" || nm == "w:moveFrom" || nm == "mc:Fallback" || nm == "w:instrText" ||
                        nm == "w:delText" || nm == "w:delInstrText");
        if (skipper) {
            if (tag.self_closing) continue;
            if (tag.closing) skip_depth = std::max(0, skip_depth - 1);
            else ++skip_depth;
            continue;
        }

        if (nm == "w:p") {
            if (tag.self_closing) continue;  // empty paragraph
            if (!tag.closing) {
                para_stack.emplace_back();
            } else if (!para_stack.empty()) {
                if (in_run) {
                    flush_run();
                    in_run = false;
                }
                push_paragraph(out, para_stack.back());
                para_stack.pop_back();
            }
        } else if (nm == "w:r") {
            if (tag.self_closing) continue;
            if (!tag.closing) {
                if (in_run) flush_run();
                in_run = true;
                run_superscript = false;
                run_text.clear();
            } else {
                flush_run();
                in_run = false;
            }
        } else if (nm == "w:rPr") {
            if (tag.self_closing) continue;
            rpr_depth += tag.closing ? -1 : 1;
            if (rpr_depth < 0) rpr_depth = 0;
        } else if (nm == "w:vertAlign") {
            if (in_run && rpr_depth > 0 && attr_value(tag.attrs, "w:val") == "superscript") run_superscript = true;
        } else if (nm == "w:t") {
            if (tag.self_closing) continue;
            in_text += tag.closing ? -1 : 1;
            if (in_text < 0) in_text = 0;
        } else if (nm == "w:tabs") {
            // Tab-stop definitions in paragraph properties, not text.
            if (!tag.self_closing) tabs_depth = std::max(0, tabs_depth + (tag.closing ? -1 : 1));
        } else if (nm == "w:tab" || nm == "w:ptab") {
            if (!tag.closing && rpr_depth == 0 && tabs_depth == 0) emit_text("\t");
        } else if (nm == "w:br" || nm == "w:cr") {
            if (tag.closing || skip_depth > 0) continue;
            if (opts.line_break_is_paragraph && !para_stack.empty()) {
                if (in_run) flush_run();
                push_paragraph(out, para_stack.back());
                para_stack.back().clear();
            } else {
                emit_text(" ");
            }
        } else if (nm == "w:noBreakHyphen") {
            if (!tag.closing) emit_text("-");
        } else if (nm == "w:sym") {
            // Symbol font characters (often checkboxes); ignore.
        }
    }
    // Unclosed paragraphs (malformed input): emit what we have.
    for (auto& p : para_stack) push_paragraph(out, p);
    return out;
}

std::vector<std::string> docx_paragraphs_from_bytes(std::string bytes, const LoadOptions& opts) {
    try {
        ZipArchive zip(std::move(bytes));
        if (!zip.contains("word/document.xml")) throw DocumentError("not a Word document (word/document.xml missing)");
        return docx_paragraphs_from_xml(zip.read("word/document.xml"), opts);
    } catch (const ZipError& e) {
        throw DocumentError(e.what());
    }
}

std::vector<std::string> text_paragraphs_from_bytes(const std::string& raw, const LoadOptions& opts) {
    std::string text;
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF && static_cast<unsigned char>(raw[1]) == 0xFE) {
        text = utf8::from_utf16(raw.substr(2), false);
    } else if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE &&
               static_cast<unsigned char>(raw[1]) == 0xFF) {
        text = utf8::from_utf16(raw.substr(2), true);
    } else if (raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        text = raw.substr(3);
    } else {
        text = raw;
    }
    std::vector<std::string> out;
    std::string block;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string norm = utf8::normalize_segment(line);
        if (opts.blank_line_paragraphs) {
            if (norm.empty()) {
                push_paragraph(out, block);
                block.clear();
            } else {
                if (!block.empty()) block.push_back(' ');
                block += norm;
            }
        } else if (!norm.empty()) {
            out.push_back(norm);
        }
        start = nl + 1;
    }
    if (opts.blank_line_paragraphs) push_paragraph(out, block);
    return out;
}

std::vector<std::string> load_paragraphs(const std::string& path, const LoadOptions& opts) {
    std::string ext = lower_extension(path);
    std::string bytes = read_file(path);
    if (ext == "docx" || ext == "docm" || ext == "dotx") {
        return docx_paragraphs_from_bytes(std::move(bytes), opts);
    }
    if (ext == "doc" || ext == "rtf" || ext == "odt" || ext == "pdf") {
        throw DocumentError("unsupported format ." + ext + " for " + path +
                            " (save it as .docx or UTF-8 .txt first)");
    }
    // A .docx renamed to something else still starts with the ZIP signature.
    if (bytes.size() > 4 && bytes.compare(0, 4, "PK\x03\x04") == 0) {
        return docx_paragraphs_from_bytes(std::move(bytes), opts);
    }
    return text_paragraphs_from_bytes(bytes, opts);
}

}  // namespace csa
