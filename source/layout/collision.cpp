#include "collision.h"
#include "../maths/ray.h"
#include "../maths/plane.h"
#include "../rendering/graphcomponentscene.h"

NodeId Collision::closestNodeToLine(const std::vector<NodeId>& nodeIds, const QVector3D &point, const QVector3D &direction)
{
    Plane plane(point, direction);
    NodeId closestNodeId;
    float minimumDistance = std::numeric_limits<float>::max();

    for(NodeId nodeId : nodeIds)
    {
        const QVector3D& position = _graphModel.nodePositions().at(nodeId) + _offset;

        if(plane.sideForPoint(position) != Plane::Side::Front)
            continue;

        float distance = position.distanceToLine(point, direction);

        if(distance < minimumDistance)
        {
            minimumDistance = distance;
            closestNodeId = nodeId;
        }
    }

    return closestNodeId;
}

NodeId Collision::closestNodeToLine(const QVector3D &point, const QVector3D &direction)
{
    return closestNodeToLine(_graphModel.graph().componentById(_componentId)->nodeIds(), point, direction);
}

void Collision::nodesIntersectingLine(const QVector3D& point, const QVector3D& direction, std::vector<NodeId>& intersectingNodeIds)
{
    Plane plane(point, direction);

    const std::vector<NodeId>& nodeIds = _graphModel.graph().componentById(_componentId)->nodeIds();
    for(NodeId nodeId : nodeIds)
    {
        const QVector3D& position = _graphModel.nodePositions().at(nodeId) + _offset;

        if(plane.sideForPoint(position) != Plane::Side::Front)
            continue;

        float distance = position.distanceToLine(point, direction);

        if(distance <= _graphModel.nodeVisuals().at(nodeId)._size)
            intersectingNodeIds.push_back(nodeId);
    }
}

NodeId Collision::nearestNodeIntersectingLine(const QVector3D& point, const QVector3D& direction)
{
    std::vector<NodeId> nodeIds;

    nodesIntersectingLine(point, direction, nodeIds);

    NodeId closestNodeId;
    float minimumDistance = std::numeric_limits<float>::max();

    for(NodeId nodeId : nodeIds)
    {
        float distance = _graphModel.nodePositions().at(nodeId).distanceToPoint(point);

        if(distance < minimumDistance)
        {
            minimumDistance = distance;
            closestNodeId = nodeId;
        }
    }

    return closestNodeId;
}
