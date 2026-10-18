#include "HtmlDocument.h"
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>
#include <cstring>
#include <algorithm>

namespace gm::engine {

static lxb_status_t SerializeCallback(const lxb_char_t* data, size_t len, void* ctx) {
    auto* s = static_cast<std::string*>(ctx);
    s->append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

static lxb_status_t SelectorFindCallback(lxb_dom_node_t* node,
                                         lxb_css_selector_specificity_t spec,
                                         void* ctx) {
    auto* nodes = static_cast<std::vector<lxb_dom_node_t*>*>(ctx);
    nodes->push_back(node);
    return LXB_STATUS_OK;
}

// HtmlNode Implementation

HtmlNode::HtmlNode(HtmlDocument* doc, lxb_dom_node* node)
    : m_doc(doc), m_node(node) {}

std::string HtmlNode::TagName() const {
    if (!m_node || m_node->type != LXB_DOM_NODE_TYPE_ELEMENT) return "";
    size_t len = 0;
    const lxb_char_t* name = lxb_dom_element_qualified_name(lxb_dom_interface_element(m_node), &len);
    return name ? std::string(reinterpret_cast<const char*>(name), len) : "";
}

std::string HtmlNode::Attr(const std::string& name) const {
    if (!m_node || m_node->type != LXB_DOM_NODE_TYPE_ELEMENT) return "";
    lxb_dom_element_t* el = lxb_dom_interface_element(m_node);
    lxb_dom_attr_t* attr = lxb_dom_element_attr_by_name(el,
        reinterpret_cast<const lxb_char_t*>(name.c_str()), name.length());
    if (!attr) return "";
    size_t len = 0;
    const lxb_char_t* val = lxb_dom_attr_value(attr, &len);
    return val ? std::string(reinterpret_cast<const char*>(val), len) : "";
}

bool HtmlNode::HasAttr(const std::string& name) const {
    if (!m_node || m_node->type != LXB_DOM_NODE_TYPE_ELEMENT) return false;
    lxb_dom_element_t* el = lxb_dom_interface_element(m_node);
    lxb_dom_attr_t* attr = lxb_dom_element_attr_by_name(el,
        reinterpret_cast<const lxb_char_t*>(name.c_str()), name.length());
    return attr != nullptr;
}

std::string HtmlNode::Text() const {
    if (!m_node) return "";
    // DOM textContent is null for a Document. Cheerio's root text() joins
    // its children, including the html element inserted by the parser.
    if (m_node->type == LXB_DOM_NODE_TYPE_DOCUMENT) {
        std::string result;
        for (auto* child = m_node->first_child; child; child = child->next)
            result += HtmlNode(m_doc, child).Text();
        return result;
    }
    size_t len = 0;
    lxb_char_t* text = lxb_dom_node_text_content(m_node, &len);
    if (!text) return "";
    std::string result(reinterpret_cast<const char*>(text), len);
    if (m_node->owner_document) {
        lxb_dom_document_destroy_text(m_node->owner_document, text);
    }
    return result;
}

std::string HtmlNode::Html() const {
    if (!m_node) return "";
    std::string out;
    lxb_html_serialize_cb(m_node, SerializeCallback, &out);
    return out;
}

HtmlNode HtmlNode::Parent() const {
    if (!m_node || !m_node->parent) return HtmlNode();
    return HtmlNode(m_doc, m_node->parent);
}

HtmlNode HtmlNode::Next() const {
    if (!m_node) return HtmlNode();
    lxb_dom_node_t* cur = m_node->next;
    while (cur && cur->type != LXB_DOM_NODE_TYPE_ELEMENT) cur = cur->next;
    return cur ? HtmlNode(m_doc, cur) : HtmlNode();
}

HtmlNode HtmlNode::Prev() const {
    if (!m_node) return HtmlNode();
    lxb_dom_node_t* cur = m_node->prev;
    while (cur && cur->type != LXB_DOM_NODE_TYPE_ELEMENT) cur = cur->prev;
    return cur ? HtmlNode(m_doc, cur) : HtmlNode();
}

std::vector<HtmlNode> HtmlNode::Children() const {
    std::vector<HtmlNode> list;
    if (!m_node) return list;
    lxb_dom_node_t* cur = m_node->first_child;
    while (cur) {
        if (cur->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            list.push_back(HtmlNode(m_doc, cur));
        }
        cur = cur->next;
    }
    return list;
}

std::vector<HtmlNode> HtmlNode::Select(const std::string& selector) const {
    if (!m_doc || !m_node) return {};
    return m_doc->SelectFrom(m_node, selector);
}

// HtmlDocument Implementation

HtmlDocument::HtmlDocument() {
    m_doc = lxb_html_document_create();
    m_cssParser = lxb_css_parser_create();
    lxb_css_parser_init(m_cssParser, nullptr);
    m_selectors = lxb_selectors_create();
    lxb_selectors_init(m_selectors);
}

HtmlDocument::~HtmlDocument() {
    if (m_selectors) {
        lxb_selectors_destroy(m_selectors, true);
        m_selectors = nullptr;
    }
    if (m_cssParser) {
        if (m_cssParser->selectors) {
            lxb_css_selectors_destroy(m_cssParser->selectors, true);
            m_cssParser->selectors = nullptr;
        }
        if (m_cssParser->memory) {
            lxb_css_memory_destroy(m_cssParser->memory, true);
            m_cssParser->memory = nullptr;
        }
        lxb_css_parser_destroy(m_cssParser, true);
        m_cssParser = nullptr;
    }
    if (m_doc) {
        lxb_html_document_destroy(m_doc);
        m_doc = nullptr;
    }
}

bool HtmlDocument::Parse(std::string_view html) {
    if (!m_doc) return false;
    lxb_status_t status = lxb_html_document_parse(m_doc,
        reinterpret_cast<const lxb_char_t*>(html.data()), html.length());
    return (status == LXB_STATUS_OK);
}

HtmlNode HtmlDocument::Root() const {
    if (!m_doc) return HtmlNode();
    return HtmlNode(const_cast<HtmlDocument*>(this), lxb_dom_interface_node(m_doc));
}

HtmlNode HtmlDocument::Body() const {
    if (!m_doc) return HtmlNode();
    lxb_html_body_element_t* body = lxb_html_document_body_element(m_doc);
    return body ? HtmlNode(const_cast<HtmlDocument*>(this), lxb_dom_interface_node(body)) : Root();
}

std::vector<HtmlNode> HtmlDocument::Select(const std::string& selector) const {
    return const_cast<HtmlDocument*>(this)->SelectFrom(lxb_dom_interface_node(m_doc), selector);
}

std::vector<HtmlNode> HtmlDocument::SelectFrom(lxb_dom_node* root, const std::string& selector) {
    if (!m_doc || !root || !m_cssParser || !m_selectors || selector.empty()) {
        return {};
    }

    std::string sel = selector;
    bool isFirst = false;
    bool isLast = false;
    std::string containsText;
    bool notContains = false;

    // Check jQuery pseudo :first
    size_t posFirst = sel.rfind(":first");
    if (posFirst != std::string::npos && (posFirst + 6 == sel.length() || sel[posFirst + 6] == ' ')) {
        isFirst = true;
        sel.erase(posFirst, 6);
    }

    // Check jQuery pseudo :last
    size_t posLast = sel.rfind(":last");
    if (posLast != std::string::npos && (posLast + 5 == sel.length() || sel[posLast + 5] == ' ')) {
        isLast = true;
        sel.erase(posLast, 5);
    }

    // Check jQuery pseudo :contains(...)
    size_t posContains = sel.find(":contains(");
    if (posContains != std::string::npos) {
        size_t endParen = sel.find(')', posContains);
        if (endParen != std::string::npos) {
            std::string inside = sel.substr(posContains + 10, endParen - (posContains + 10));
            if (inside.size() >= 2 && (inside.front() == '"' || inside.front() == '\'')) {
                inside = inside.substr(1, inside.length() - 2);
            }
            containsText = inside;

            // Check if preceded by :not
            if (posContains >= 5 && sel.compare(posContains - 5, 5, ":not(") == 0) {
                notContains = true;
                size_t outerParen = sel.find(')', endParen + 1);
                if (outerParen != std::string::npos) {
                    sel.erase(posContains - 5, outerParen + 1 - (posContains - 5));
                } else {
                    sel.erase(posContains - 5, (endParen + 1) - (posContains - 5));
                }
            } else {
                sel.erase(posContains, endParen + 1 - posContains);
            }
        }
    }

    // Trim whitespace
    size_t start = sel.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        sel = "*";
    } else {
        size_t end = sel.find_last_not_of(" \t\r\n");
        sel = sel.substr(start, end - start + 1);
        if (sel.empty()) sel = "*";
    }

    lxb_css_selector_list_t* list = lxb_css_selectors_parse(m_cssParser,
        reinterpret_cast<const lxb_char_t*>(sel.c_str()), sel.length());
    if (!list || m_cssParser->status != LXB_STATUS_OK) {
        return {};
    }

    std::vector<lxb_dom_node_t*> matchedNodes;
    lxb_status_t status = lxb_selectors_find(m_selectors, root, list, SelectorFindCallback, &matchedNodes);
    lxb_css_selector_list_destroy_chain(list);
    lxb_css_parser_erase(m_cssParser);
    lxb_selectors_clean(m_selectors);

    if (status != LXB_STATUS_OK || matchedNodes.empty()) {
        return {};
    }

    std::vector<HtmlNode> results;
    results.reserve(matchedNodes.size());

    for (lxb_dom_node_t* n : matchedNodes) {
        HtmlNode node(this, n);
        if (!containsText.empty()) {
            std::string t = node.Text();
            bool has = (t.find(containsText) != std::string::npos);
            if (notContains ? has : !has) {
                continue;
            }
        }
        results.push_back(node);
    }

    if (isFirst && !results.empty()) {
        return { results.front() };
    }
    if (isLast && !results.empty()) {
        return { results.back() };
    }

    return results;
}

} // namespace gm::engine
