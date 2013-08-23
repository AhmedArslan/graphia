#ifndef GRAPH_H
#define GRAPH_H

#include <QObject>
#include <QList>
#include <QSet>
#include <QVector>
#include <QQueue>
#include <QDebug>

class ResizableGraphArray;

typedef int NodeId;
const static NodeId NullNodeId = -1;
typedef int EdgeId;
const static EdgeId NullEdgeId = -1;

class Node
{
    friend class Graph;

private:
    NodeId _id;
    QSet<EdgeId> _inEdges;
    QSet<EdgeId> _outEdges;

public:
    Node() : _id(NullNodeId) {}
    Node(const Node& other) :
        _id(other._id),
        _inEdges(other._inEdges),
        _outEdges(other._outEdges)
    {}

    const QSet<EdgeId> inEdges() const { return _inEdges; }
    int inDegree() const { return _inEdges.size(); }
    const QSet<EdgeId> outEdges() const { return _outEdges; }
    int outDegree() const { return _outEdges.size(); }
    const QSet<EdgeId> edges() const { return _inEdges + _outEdges; }
    int degree() const { return edges().size(); }

    NodeId id() const { return _id; }
};

class Edge
{
    friend class Graph;

private:
    EdgeId _id;
    NodeId _sourceId;
    NodeId _targetId;

public:
    Edge() :
        _id(NullNodeId),
        _sourceId(NullNodeId),
        _targetId(NullNodeId)
    {}
    Edge(const Edge& other) :
        _id(other._id),
        _sourceId(other._sourceId),
        _targetId(other._targetId)
    {}

    NodeId sourceId() const { return _sourceId; }
    NodeId targetId() const { return _targetId; }

    bool isLoop() const { return sourceId() == targetId(); }

    EdgeId id() const { return _id; }
};

class ReadonlyGraph
{
    virtual const QList<NodeId>& nodeIds() const = 0;
    virtual int numNodes() const = 0;
    virtual const Node& nodeById(NodeId nodeId) const = 0;

    virtual const QList<EdgeId>& edgeIds() const = 0;
    virtual int numEdges() const = 0;
    virtual const Edge& edgeById(EdgeId edgeId) const = 0;
};

class Graph : public QObject, public ReadonlyGraph
{
    Q_OBJECT
public:
    Graph();
    virtual ~Graph();

private:
    QList<NodeId> nodeIdsList;
    QVector<Node> nodesVector;
    NodeId nextNodeId;
    QQueue<NodeId> vacatedNodeIdQueue;

    QList<EdgeId> edgeIdsList;
    QVector<Edge> edgesVector;
    EdgeId nextEdgeId;
    QQueue<EdgeId> vacatedEdgeIdQueue;

    template<typename> friend class NodeArray;
    QList<ResizableGraphArray*> nodeArrayList;
    int nodeArrayCapacity() const { return nextNodeId; }

    template<typename> friend class EdgeArray;
    QList<ResizableGraphArray*> edgeArrayList;
    int edgeArrayCapacity() const { return nextEdgeId; }

public:
    const QList<NodeId>& nodeIds() const { return nodeIdsList; }
    int numNodes() const { return nodeIdsList.size(); }
    const Node& nodeById(NodeId nodeId) const { return nodesVector[nodeId]; }

    NodeId addNode();
    void removeNode(NodeId nodeId);

    const QList<EdgeId>& edgeIds() const { return edgeIdsList; }
    int numEdges() const { return edgeIdsList.size(); }
    const Edge& edgeById(EdgeId edgeId) const { return edgesVector[edgeId]; }

    EdgeId addEdge(NodeId sourceId, NodeId targetId);
    void removeEdge(EdgeId edgeId); 

    void setEdgeNodes(Edge& edge, NodeId sourceId, NodeId targetId);
    void setEdgeNodes(EdgeId edgeId, NodeId sourceId, NodeId targetId);

    void dumpToQDebug(int detail) const
    {
        qDebug() << numNodes() << "nodes" << numEdges() << "edges";

        if(detail > 0)
        {
            for(NodeId nodeId : nodeIds())
                qDebug() << "Node" << nodeId;

            for(EdgeId edgeId : edgeIds())
            {
                const Edge& edge = edgeById(edgeId);
                qDebug() << "Edge" << edgeId << "(" << edge.sourceId() << "->" << edge.targetId() << ")";
            }
        }
    }

signals:
    void graphWillChange(Graph&);
    void graphChanged(Graph&);
    void nodeAdded(Graph&, NodeId);
    void nodeWillBeRemoved(Graph&, NodeId);
    void edgeAdded(Graph&, EdgeId);
    void edgeWillBeRemoved(Graph&, EdgeId);
};

#endif // GRAPH_H
