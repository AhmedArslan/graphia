/* Copyright © 2013-2020 Graphia Technologies Ltd.
 *
 * This file is part of Graphia.
 *
 * Graphia is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Graphia is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Graphia.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COMPONENTMANAGER_H
#define COMPONENTMANAGER_H

#include "shared/graph/grapharray.h"

#include "graphfilter.h"

#include <map>
#include <queue>
#include <mutex>
#include <vector>
#include <functional>
#include <algorithm>
#include <memory>

#include <QObject>
#include <QtGlobal>

class Graph;
class GraphComponent;

class ComponentSplitSet
{
private:
    ComponentId _oldComponentId;
    ComponentIdSet _splitters;

public:
    ComponentSplitSet(ComponentId oldComponentId, ComponentIdSet&& splitters) :
        _oldComponentId(oldComponentId), _splitters(splitters)
    {
        Q_ASSERT(!oldComponentId.isNull());
        Q_ASSERT(!std::any_of(_splitters.begin(), _splitters.end(),
                 [](const auto& splitter) { return splitter.isNull(); }));
    }

    ComponentId oldComponentId() const { return _oldComponentId; }
    const ComponentIdSet& splitters() const { return _splitters; }
};

class ComponentMergeSet
{
private:
    ComponentIdSet _mergers;
    ComponentId _newComponentId;

public:
    ComponentMergeSet(ComponentIdSet&& mergers, ComponentId newComponentId) :
        _mergers(mergers), _newComponentId(newComponentId)
    {
        Q_ASSERT(!newComponentId.isNull());
        Q_ASSERT(!std::any_of(_mergers.begin(), _mergers.end(),
                 [](const auto& merger) { return merger.isNull(); }));
    }

    const ComponentIdSet& mergers() const { return _mergers; }
    ComponentId newComponentId() const { return _newComponentId; }
};

class ComponentManager : public QObject, public GraphFilter
{
    friend class Graph;

    Q_OBJECT
public:
    explicit ComponentManager(Graph& graph,
                     const NodeConditionFn& nodeFilter = nullptr,
                     const EdgeConditionFn& edgeFilter = nullptr);
    ~ComponentManager() override;

private:
    std::vector<ComponentId> _componentIds;
    ComponentId _nextComponentId;
    std::queue<ComponentId> _vacatedComponentIdQueue;
    std::map<ComponentId, std::unique_ptr<GraphComponent>> _componentsMap;
    ComponentIdSet _updatesRequired;
    NodeArray<ComponentId> _nodesComponentId;
    EdgeArray<ComponentId> _edgesComponentId;

    mutable std::recursive_mutex _updateMutex;

    std::mutex _componentArraysMutex;
    std::unordered_set<IGraphArray*> _componentArrays;

    bool _enabled = true;
    bool _debug = false;

    ComponentId generateComponentId();
    void queueGraphComponentUpdate(const Graph* graph, ComponentId componentId);
    void updateGraphComponents(const Graph* graph);
    void removeGraphComponent(ComponentId componentId);

    void update(const Graph* graph);
    int componentArrayCapacity() const { return static_cast<int>(_nextComponentId); }
    ComponentIdSet assignConnectedElementsComponentId(const Graph* graph, NodeId rootId, ComponentId componentId,
                                                      NodeArray<ComponentId>& nodesComponentId,
                                                      EdgeArray<ComponentId>& edgesComponentId);

    void insertComponentArray(IGraphArray* componentArray);
    void eraseComponentArray(IGraphArray* componentArray);

private slots:
    void onGraphChanged(const Graph* graph, bool changeOccurred);

public:
    const std::vector<ComponentId>& componentIds() const;
    int numComponents() const { return static_cast<int>(componentIds().size()); }
    bool containsComponentId(ComponentId componentId) const;
    const GraphComponent* componentById(ComponentId componentId) const;
    ComponentId componentIdOfNode(NodeId nodeId) const;
    ComponentId componentIdOfEdge(EdgeId edgeId) const;

    void enable() { _enabled = true; }
    void disable() { _enabled = false; }
    bool enabled() const { return _enabled; }

    void enableDebug() { _debug = true; }
    void disbleDebug() { _debug = false; }

signals:
    void componentAdded(const Graph*, ComponentId, bool) const;
    void componentWillBeRemoved(const Graph*, ComponentId, bool) const;
    void componentSplit(const Graph*, const ComponentSplitSet&) const;
    void componentsWillMerge(const Graph*, const ComponentMergeSet&) const;

    void nodeRemovedFromComponent(const Graph*, NodeId, ComponentId) const;
    void edgeRemovedFromComponent(const Graph*, EdgeId, ComponentId) const;
    void nodeAddedToComponent(const Graph*, NodeId, ComponentId) const;
    void edgeAddedToComponent(const Graph*, EdgeId, ComponentId) const;
};

#endif // COMPONENTMANAGER_H
