#pragma once

#include "JsEngine.h"
#include "HtmlDocument.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>

namespace gm::engine {

struct DocContext {
    std::unique_ptr<HtmlDocument> doc;
    std::vector<lxb_dom_node*>    nodes; // Index is nodeId
    std::unordered_map<lxb_dom_node*, int> nodeToId;

    int GetOrAddNode(lxb_dom_node* n) {
        if (!n) return -1;
        auto it = nodeToId.find(n);
        if (it != nodeToId.end()) return it->second;
        int id = (int)nodes.size();
        nodes.push_back(n);
        nodeToId[n] = id;
        return id;
    }
};

class JsBridge {
public:
    JsBridge() = default;
    ~JsBridge() = default;

    void Install(JsEngine& engine);

    // Document handle manager
    int RegisterDoc(std::unique_ptr<HtmlDocument> doc);
    void ReleaseDoc(int docId);
    void ReleaseAllDocs();
    DocContext* GetDoc(int docId);

private:
    std::mutex m_docMutex;
    int m_nextDocId = 1;
    std::unordered_map<int, DocContext> m_docs;
};

} // namespace gm::engine
