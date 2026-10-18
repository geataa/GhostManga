#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <memory>

struct lxb_html_document;
struct lxb_dom_node;
struct lxb_css_parser;
struct lxb_selectors;

namespace gm::engine {

class HtmlDocument;

class HtmlNode {
public:
    HtmlNode() = default;
    HtmlNode(HtmlDocument* doc, lxb_dom_node* node);

    bool IsValid() const { return m_node != nullptr; }

    std::string TagName() const;
    std::string Attr(const std::string& name) const;
    bool HasAttr(const std::string& name) const;
    std::string Text() const;
    std::string Html() const;

    HtmlNode Parent() const;
    HtmlNode Next() const;
    HtmlNode Prev() const;
    std::vector<HtmlNode> Children() const;

    // Run CSS selector within this subtree
    std::vector<HtmlNode> Select(const std::string& selector) const;

    lxb_dom_node* Raw() const { return m_node; }
    HtmlDocument* Doc() const { return m_doc; }

private:
    HtmlDocument* m_doc = nullptr;
    lxb_dom_node* m_node = nullptr;
};

class HtmlDocument {
public:
    HtmlDocument();
    ~HtmlDocument();

    HtmlDocument(const HtmlDocument&) = delete;
    HtmlDocument& operator=(const HtmlDocument&) = delete;

    bool Parse(std::string_view html);

    HtmlNode Root() const;
    HtmlNode Body() const;

    std::vector<HtmlNode> Select(const std::string& selector) const;

    // Internal helper for node selection
    std::vector<HtmlNode> SelectFrom(lxb_dom_node* root, const std::string& selector);

private:
    lxb_html_document* m_doc = nullptr;
    lxb_css_parser*    m_cssParser = nullptr;
    lxb_selectors*     m_selectors = nullptr;
};

} // namespace gm::engine
