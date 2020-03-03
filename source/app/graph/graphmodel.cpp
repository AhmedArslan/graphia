#include "graphmodel.h"
#include "componentmanager.h"

#include "graph/mutablegraph.h"
#include "shared/graph/grapharray.h"

#include "layout/nodepositions.h"

#include "ui/selectionmanager.h"
#include "ui/searchmanager.h"

#include "attributes/attribute.h"

#include "transform/transformedgraph.h"
#include "transform/transforminfo.h"
#include "transform/transforms/filtertransform.h"
#include "transform/transforms/edgecontractiontransform.h"
#include "transform/transforms/mcltransform.h"
#include "transform/transforms/louvaintransform.h"
#include "transform/transforms/pageranktransform.h"
#include "transform/transforms/eccentricitytransform.h"
#include "transform/transforms/betweennesstransform.h"
#include "transform/transforms/contractbyattributetransform.h"
#include "transform/transforms/separatebyattributetransform.h"
#include "transform/transforms/knntransform.h"
#include "transform/transforms/percentnntransform.h"
#include "transform/transforms/edgereductiontransform.h"
#include "transform/transforms/spanningtreetransform.h"
#include "transform/transforms/attributesynthesistransform.h"
#include "transform/transforms/combineattributestransform.h"
#include "transform/transforms/conditionalattributetransform.h"
#include "transform/transforms/removeleavestransform.h"
#include "transform/graphtransformconfigparser.h"

#include "ui/visualisations/colorvisualisationchannel.h"
#include "ui/visualisations/sizevisualisationchannel.h"
#include "ui/visualisations/textvisualisationchannel.h"
#include "ui/visualisations/visualisationconfigparser.h"
#include "ui/visualisations/visualisationbuilder.h"
#include "ui/visualisations/visualisationinfo.h"
#include "ui/visualisations/elementvisual.h"

#include "shared/plugins/iplugin.h"
#include "shared/commands/icommand.h"

#include "shared/utils/enumreflection.h"
#include "shared/utils/preferences.h"
#include "shared/utils/utils.h"
#include "shared/utils/pair_iterator.h"
#include "shared/utils/flags.h"
#include "shared/utils/string.h"

#include <QRegularExpression>

#include <utility>

using NodeVisuals = NodeArray<ElementVisual>;
using EdgeVisuals = EdgeArray<ElementVisual>;

class GraphModelImpl
{
    friend class GraphModel;

public:
    explicit GraphModelImpl(GraphModel& graphModel) :
        _transformedGraph(graphModel, _graph),
        _nodePositions(_graph),
        _nodeVisuals(_graph),
        _edgeVisuals(_graph),
        _mappedNodeVisuals(_graph),
        _mappedEdgeVisuals(_graph),
        _nodeNames(_graph)
    {}

private:
    MutableGraph _graph;
    TransformedGraph _transformedGraph;
    TransformInfosMap _transformInfos;
    NodePositions _nodePositions;

    NodeVisuals _nodeVisuals;
    EdgeVisuals _edgeVisuals;
    NodeVisuals _mappedNodeVisuals;
    EdgeVisuals _mappedEdgeVisuals;
    VisualisationInfosMap _visualisationInfos;

    NodeArray<QString> _nodeNames;

    std::map<QString, Attribute> _attributes;
    std::vector<QString> _previousDynamicAttributeNames;
    std::map<QString, std::unique_ptr<GraphTransformFactory>> _graphTransformFactories;

    std::map<QString, std::unique_ptr<VisualisationChannel>> _visualisationChannels;

    // Slightly hacky state variable that tracks whether or not a edge text
    // visualisation is present. This is required so that we can show a warning
    // to the user, if they have edge text enabled, but do not have a
    // corresponding visualisation
    bool _hasValidEdgeTextVisualisation = false;

    NodeIdSet _selectedNodeIds;
    NodeIdSet _foundNodeIds;
    NodeIdSet _highlightedNodeIds;

    bool _nodesMaskActive = false;
};

GraphModel::GraphModel(QString name, IPlugin* plugin) :
    _(std::make_unique<GraphModelImpl>(*this)),
    _transformedGraphIsChanging(false),
    _name(std::move(name)),
    _plugin(plugin)
{
    connect(&_->_transformedGraph, &Graph::nodeRemoved, [this](const Graph*, NodeId nodeId)
    {
       _->_nodeVisuals[nodeId]._state = VisualFlags::None;
    });
    connect(&_->_transformedGraph, &Graph::edgeRemoved, [this](const Graph*, EdgeId edgeId)
    {
       _->_edgeVisuals[edgeId]._state = VisualFlags::None;
    });

    connect(&_->_graph, &Graph::graphChanged, this, &GraphModel::onMutableGraphChanged, Qt::DirectConnection);

    connect(&_->_transformedGraph, &Graph::graphWillChange, this, &GraphModel::onTransformedGraphWillChange, Qt::DirectConnection);
    connect(&_->_transformedGraph, &Graph::graphChanged, this, &GraphModel::onTransformedGraphChanged, Qt::DirectConnection);
    connect(&_->_transformedGraph, &TransformedGraph::attributeValuesChanged, this,
        &GraphModel::attributeValuesChanged, Qt::DirectConnection);

    connect(S(Preferences), &Preferences::preferenceChanged, this, &GraphModel::onPreferenceChanged);

    GraphModel::createAttribute(tr("Node Degree"))
        .setIntValueFn([this](NodeId nodeId) { return _->_transformedGraph.nodeById(nodeId).degree(); })
        .intRange().setMin(0)
        .setDescription(tr("A node's degree is its number of incident edges."));

    if(directed())
    {
        GraphModel::createAttribute(tr("Node In Degree"))
            .setIntValueFn([this](NodeId nodeId) { return _->_transformedGraph.nodeById(nodeId).inDegree(); })
            .intRange().setMin(0)
            .setDescription(tr("A node's in degree is its number of inbound edges."));

        GraphModel::createAttribute(tr("Node Out Degree"))
            .setIntValueFn([this](NodeId nodeId) { return _->_transformedGraph.nodeById(nodeId).outDegree(); })
            .intRange().setMin(0)
            .setDescription(tr("A node's out degree is its number of outbound edges."));
    }

    GraphModel::createAttribute(tr("Node Multiplicity"))
        .setIntValueFn([this](NodeId nodeId) { return _->_transformedGraph.multiplicityOf(nodeId); })
        .intRange().setMin(0)
        .setDescription(tr("A node's multiplicity is how many nodes it represents."));

    GraphModel::createAttribute(tr("Edge Multiplicity"))
        .setIntValueFn([this](EdgeId edgeId) { return _->_transformedGraph.multiplicityOf(edgeId); })
        .intRange().setMin(0)
        .setDescription(tr("An edge's multiplicity is how many edges it represents."));

    GraphModel::createAttribute(tr("Component Size"))
        .setIntValueFn([](const IGraphComponent& component) { return component.numNodes(); })
        .intRange().setMin(1)
        .setDescription(tr("Component Size refers to the number of nodes the component contains."));

    GraphModel::createAttribute(tr("Node Component Identifier"))
        .setStringValueFn([this](NodeId nodeId)
        {
            return QStringLiteral("Component %1").arg(static_cast<int>(_->_transformedGraph.componentIdOfNode(nodeId) + 1));
        })
        .setDescription(tr("A node's component identifier indicates which component it is part of."))
        .setFlag(AttributeFlag::DisableDuringTransfom);

    GraphModel::createAttribute(tr("Edge Component Identifier"))
        .setStringValueFn([this](EdgeId edgeId)
        {
            return QStringLiteral("Component %1").arg(static_cast<int>(_->_transformedGraph.componentIdOfEdge(edgeId) + 1));
        })
        .setDescription(tr("An edge's component identifier indicates which component it is part of."))
        .setFlag(AttributeFlag::DisableDuringTransfom);

    _->_graphTransformFactories.emplace(tr("Remove Nodes"),             std::make_unique<FilterTransformFactory>(this, ElementType::Node, false));
    _->_graphTransformFactories.emplace(tr("Remove Edges"),             std::make_unique<FilterTransformFactory>(this, ElementType::Edge, false));
    _->_graphTransformFactories.emplace(tr("Remove Components"),        std::make_unique<FilterTransformFactory>(this, ElementType::Component, false));
    _->_graphTransformFactories.emplace(tr("Keep Nodes"),               std::make_unique<FilterTransformFactory>(this, ElementType::Node, true));
    _->_graphTransformFactories.emplace(tr("Keep Edges"),               std::make_unique<FilterTransformFactory>(this, ElementType::Edge, true));
    _->_graphTransformFactories.emplace(tr("Keep Components"),          std::make_unique<FilterTransformFactory>(this, ElementType::Component, true));
    _->_graphTransformFactories.emplace(tr("Contract Edges"),           std::make_unique<EdgeContractionTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("MCL Cluster"),              std::make_unique<MCLTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Louvain Cluster"),          std::make_unique<LouvainTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Weighted Louvain Cluster"), std::make_unique<WeightedLouvainTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("PageRank"),                 std::make_unique<PageRankTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Eccentricity"),             std::make_unique<EccentricityTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Betweenness"),              std::make_unique<BetweennessTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Contract By Attribute"),    std::make_unique<ContractByAttributeTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Separate By Attribute"),    std::make_unique<SeparateByAttributeTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Boolean Node Attribute"),   std::make_unique<ConditionalAttributeTransformFactory>(this, ElementType::Node));
    _->_graphTransformFactories.emplace(tr("Boolean Edge Attribute"),   std::make_unique<ConditionalAttributeTransformFactory>(this, ElementType::Edge));
    _->_graphTransformFactories.emplace(tr("k-NN"),                     std::make_unique<KNNTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("%-NN"),                     std::make_unique<PercentNNTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Edge Reduction"),           std::make_unique<EdgeReductionTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Spanning Forest"),          std::make_unique<SpanningTreeTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Attribute Synthesis"),      std::make_unique<AttributeSynthesisTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Combine Attributes"),       std::make_unique<CombineAttributesTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Remove Leaves"),            std::make_unique<RemoveLeavesTransformFactory>(this));
    _->_graphTransformFactories.emplace(tr("Remove Branches"),          std::make_unique<RemoveBranchesTransformFactory>(this));

    _->_visualisationChannels.emplace(tr("Colour"), std::make_unique<ColorVisualisationChannel>());
    _->_visualisationChannels.emplace(tr("Size"), std::make_unique<SizeVisualisationChannel>());
    _->_visualisationChannels.emplace(tr("Text"), std::make_unique<TextVisualisationChannel>());
}

GraphModel::~GraphModel() // NOLINT
{
    // Only here so that we can have a unique_ptr to GraphModelImpl
}

void GraphModel::removeDynamicAttributes()
{
    QStringList dynamicAttributeNames;

    for(const auto& attributePair : _->_attributes)
    {
        if(attributePair.second.testFlag(AttributeFlag::Dynamic))
            dynamicAttributeNames.append(attributePair.first);
    }

    for(const auto& attributeName : dynamicAttributeNames)
        _->_attributes.erase(attributeName);
}

QString GraphModel::normalisedAttributeName(QString attribute) const
{
    // Dots in attribute names are disallowed as they conflict with
    // the dot syntax for parameterised attributes
    attribute.replace('.', '_');

    while(u::contains(_->_attributes, attribute))
    {
        int number = 1;

        // The attribute name is already used, so generate a new one
        QRegularExpression re(QStringLiteral(R"(^(.*)\((\d+)\)$)"));
        auto match = re.match(attribute);
        if(match.hasMatch())
        {
            attribute = match.captured(1);
            number = match.captured(2).toInt() + 1;
        }

        attribute = QStringLiteral("%1(%2)").arg(attribute).arg(number);
    }

    return attribute;
}

IMutableGraph& GraphModel::mutableGraphImpl() { return mutableGraph(); }
const IMutableGraph& GraphModel::mutableGraphImpl() const { return mutableGraph(); }
const IGraph& GraphModel::graphImpl() const { return graph(); }

const IElementVisual& GraphModel::nodeVisualImpl(NodeId nodeId) const { return nodeVisual(nodeId); }
const IElementVisual& GraphModel::edgeVisualImpl(EdgeId edgeId) const { return edgeVisual(edgeId); }

MutableGraph& GraphModel::mutableGraph() { return _->_graph; }
const MutableGraph& GraphModel::mutableGraph() const { return _->_graph; }
const Graph& GraphModel::graph() const { return _->_transformedGraph; }

const ElementVisual& GraphModel::nodeVisual(NodeId nodeId) const { return _->_nodeVisuals.at(nodeId); }
const ElementVisual& GraphModel::edgeVisual(EdgeId edgeId) const { return _->_edgeVisuals.at(edgeId); }

NodePositions& GraphModel::nodePositions() { return _->_nodePositions; }
const NodePositions& GraphModel::nodePositions() const { return _->_nodePositions; }

const NodeArray<QString>& GraphModel::nodeNames() const { return _->_nodeNames; }
QString GraphModel::nodeName(NodeId nodeId) const { return _->_nodeNames[nodeId]; }
void GraphModel::setNodeName(NodeId nodeId, const QString& name)
{
    _->_nodeNames[nodeId] = name;
    updateVisuals();
}

bool GraphModel::editable() const { return _plugin->editable(); }
bool GraphModel::directed() const { return _plugin->directed(); }

QString GraphModel::pluginName() const { return _plugin->name(); }
int GraphModel::pluginDataVersion() const { return _plugin->dataVersion(); }
QString GraphModel::pluginQmlPath() const { return _plugin->qmlPath(); }

bool GraphModel::graphTransformIsValid(const QString& transform) const
{
    GraphTransformConfigParser p;
    bool parsed = p.parse(transform, false);

    if(parsed)
    {
        const auto& graphTransformConfig = p.result();

        if(!u::contains(_->_graphTransformFactories, graphTransformConfig._action))
            return false;

        auto& factory = _->_graphTransformFactories.at(graphTransformConfig._action);

        if(factory->requiresCondition() && !graphTransformConfig.hasCondition())
            return false;

        return factory->configIsValid(graphTransformConfig);
    }

    return false;
}

QStringList GraphModel::transformsWithMissingParametersSetToDefault(const QStringList& transforms) const
{
    QStringList transformsWithDefaults;

    for(const auto& transform : transforms)
    {
        GraphTransformConfigParser graphTransformConfigParser;

        if(!graphTransformConfigParser.parse(transform))
            continue;

        auto graphTransformConfig = graphTransformConfigParser.result();

        // This can happen if the file has transforms that the app doesn't
        if(!u::contains(_->_graphTransformFactories, graphTransformConfig._action))
            continue;

        const auto& factory = _->_graphTransformFactories.at(graphTransformConfig._action);

        factory->setMissingParametersToDefault(graphTransformConfig);
        transformsWithDefaults.append(graphTransformConfig.asString());
    }

    return transformsWithDefaults;
}

void GraphModel::buildTransforms(const QStringList& transforms, ICommand* command)
{
    _->_transformedGraph.clearTransforms();
    _->_transformedGraph.setCommand(command);
    _->_transformInfos.clear();
    for(int index = 0; index < transforms.size(); index++)
    {
        const auto& transform = transforms.at(index);
        GraphTransformConfigParser graphTransformConfigParser;

        if(!graphTransformConfigParser.parse(transform))
            continue;

        const auto& graphTransformConfig = graphTransformConfigParser.result();
        const auto& action = graphTransformConfig._action;

        if(graphTransformConfig.isFlagSet(QStringLiteral("disabled")))
            continue;

        if(!u::contains(_->_graphTransformFactories, action))
            continue;

        const auto& factory = _->_graphTransformFactories.at(action);
        auto graphTransform = factory->create(graphTransformConfig);

        Q_ASSERT(graphTransform != nullptr);
        if(graphTransform != nullptr)
        {
            graphTransform->setIndex(index);
            graphTransform->setConfig(graphTransformConfig);
            graphTransform->setRepeating(graphTransformConfig.isFlagSet(QStringLiteral("repeating")));
            graphTransform->setInfo(&_->_transformInfos[index]);
            _->_transformedGraph.addTransform(std::move(graphTransform));
        }
    }

    _->_transformedGraph.enableAutoRebuild();
    _->_transformedGraph.setCommand(nullptr);
}

void GraphModel::cancelTransformBuild()
{
    _->_transformedGraph.cancelRebuild();
}

QStringList GraphModel::availableTransformNames() const
{
    QStringList stringList;
    stringList.reserve(static_cast<int>(_->_graphTransformFactories.size()));

    for(auto& t : _->_graphTransformFactories)
    {
        auto elementType = _->_graphTransformFactories.at(t.first)->elementType();
        bool attributesAvailable = !availableAttributeNames(elementType, ValueType::All).isEmpty();

        if(elementType == ElementType::None || attributesAvailable)
            stringList.append(t.first);
    }

    return stringList;
}

const GraphTransformFactory* GraphModel::transformFactory(const QString& transformName) const
{
    if(!transformName.isEmpty() && u::contains(_->_graphTransformFactories, transformName))
        return _->_graphTransformFactories.at(transformName).get();

    return nullptr;
}

QStringList GraphModel::availableAttributeNames(ElementType elementTypes,
    ValueType valueTypes, AttributeFlag skipFlags) const
{
    QStringList stringList;

    for(auto& attribute : _->_attributes)
    {
        if(!Flags<ElementType>(elementTypes).test(attribute.second.elementType()))
            continue;

        if(!Flags<ValueType>(valueTypes).test(attribute.second.valueType()))
            continue;

        if(attribute.second.testFlag(skipFlags))
            continue;

        stringList.append(attribute.first);
    }

    return stringList;
}

QStringList GraphModel::avaliableConditionFnOps(const QString& attributeName) const
{
    QStringList ops;

    if(attributeName.isEmpty() || !u::contains(_->_attributes, attributeName))
        return ops;

    const auto& attribute = _->_attributes.at(attributeName);
    ops.append(GraphTransformConfigParser::ops(attribute.valueType()));

    if(attribute.hasMissingValues())
        ops.append(GraphTransformConfigParser::opToString(ConditionFnOp::Unary::HasValue));

    return ops;
}

bool GraphModel::hasTransformInfo() const
{
    return !_->_transformInfos.empty();
}

const TransformInfo& GraphModel::transformInfoAtIndex(int index) const
{
    static TransformInfo nullTransformInfo;

    if(u::contains(_->_transformInfos, index))
        return _->_transformInfos.at(index);

    return nullTransformInfo;
}

std::vector<QString> GraphModel::createdAttributeNamesAtTransformIndex(int index) const
{
    return _->_transformedGraph.createdAttributeNamesAtTransformIndex(index);
}

std::vector<QString> GraphModel::createdAttributeNamesAtTransformIndexOrLater(int firstIndex) const
{
    std::vector<QString> attributeNames;

    for(int index = firstIndex; index < _->_transformedGraph.numTransforms(); index++)
    {
        auto createdAttributeNames = createdAttributeNamesAtTransformIndex(index);
        attributeNames.insert(attributeNames.end(),
            createdAttributeNames.begin(), createdAttributeNames.end());
    }

    return attributeNames;
}

bool GraphModel::opIsUnary(const QString& op)
{
    return GraphTransformConfigParser::opIsUnary(op);
}

bool GraphModel::visualisationIsValid(const QString& visualisation) const
{
    VisualisationConfigParser p;
    bool parsed = p.parse(visualisation, false);

    if(parsed)
    {
        const auto& visualisationConfig = p.result();

        if(!attributeExists(visualisationConfig._attributeName))
            return false;

        if(!u::contains(_->_visualisationChannels, visualisationConfig._channelName))
            return false;

        return true;
    }

    return false;
}

void GraphModel::buildVisualisations(const QStringList& visualisations)
{
    _->_mappedNodeVisuals.resetElements();
    _->_mappedEdgeVisuals.resetElements();
    clearVisualisationInfos();

    _->_hasValidEdgeTextVisualisation = false;

    VisualisationsBuilder<NodeId> nodeVisualisationsBuilder(graph(), _->_mappedNodeVisuals);
    VisualisationsBuilder<EdgeId> edgeVisualisationsBuilder(graph(), _->_mappedEdgeVisuals);

    for(int index = 0; index < visualisations.size(); index++)
    {
        const auto& visualisation = visualisations.at(index);

        VisualisationConfigParser visualisationConfigParser;

        if(!visualisationConfigParser.parse(visualisation))
            continue;

        const auto& visualisationConfig = visualisationConfigParser.result();

        if(visualisationConfig.isFlagSet(QStringLiteral("disabled")))
            continue;

        const auto& attributeName = visualisationConfig._attributeName;
        const auto& channelName = visualisationConfig._channelName;
        auto& info = _->_visualisationInfos[index];

        if(!attributeExists(attributeName))
        {
            info.addAlert(AlertType::Error, tr("Attribute '%1' doesn't exist").arg(attributeName));
            continue;
        }

        if(!u::contains(_->_visualisationChannels, channelName))
            continue;

        auto attribute = attributeValueByName(attributeName);
        auto& channel = _->_visualisationChannels.at(channelName);

        channel->findErrors(info);

        if(!channel->supports(attribute.valueType()))
        {
            info.addAlert(AlertType::Error, tr("Visualisation doesn't support attribute type"));
            continue;
        }

        channel->reset();

        for(const auto& parameter : visualisationConfig._parameters)
            channel->setParameter(parameter._name, parameter.valueAsString());

        if(attribute.elementType() == ElementType::Edge && channelName == QStringLiteral("Text"))
            _->_hasValidEdgeTextVisualisation = true;

        if(attribute.valueType() == ValueType::String)
        {
            QCollator collator;
            collator.setNumericMode(true);

            auto sharedValues = attribute.sharedValues();
            if(sharedValues.empty())
            {
                if(attribute.elementType() == ElementType::Node)
                    sharedValues = attribute.findSharedValuesForElements(graph().nodeIds());
                else if(attribute.elementType() == ElementType::Edge)
                    sharedValues = attribute.findSharedValuesForElements(graph().edgeIds());
            }

            if(!visualisationConfig.isFlagSet(QStringLiteral("assignByQuantity")))
            {
                // Sort in natural order so that e.g. "Thing 1" is always
                // assigned a visualisation before "Thing 2"
                std::sort(sharedValues.begin(), sharedValues.end(),
                [&collator](const auto& a, const auto& b)
                {
                    return collator.compare(a._value, b._value) < 0;
                });
            }
            else
            {
                // Shared values should already be sorted at this point,
                // but resort anyway as in that case it'll be cheap and
                // if it's not sorted (for whatever reason), we need it
                // to be sorted
                std::sort(sharedValues.begin(), sharedValues.end(),
                [&collator](const auto& a, const auto& b)
                {
                    if(a._count == b._count)
                        return collator.compare(a._value, b._value) < 0;

                    return a._count > b._count;
                });
            }

            for(const auto& sharedValue : sharedValues)
            {
                channel->addValue(sharedValue._value);
                info.addStringValue(sharedValue._value);
            }
        }

        switch(attribute.elementType())
        {
        case ElementType::Node:
            nodeVisualisationsBuilder.build(attribute, *channel, visualisationConfig, index, info);
            break;

        case ElementType::Edge:
            edgeVisualisationsBuilder.build(attribute, *channel, visualisationConfig, index, info);
            break;

        default:
            break;
        }
    }

    nodeVisualisationsBuilder.findOverrideAlerts(_->_visualisationInfos);
    edgeVisualisationsBuilder.findOverrideAlerts(_->_visualisationInfos);

    updateVisuals();
}

bool GraphModel::hasValidEdgeTextVisualisation() const
{
    return _->_hasValidEdgeTextVisualisation;
}

QStringList GraphModel::availableVisualisationChannelNames(ValueType valueType) const
{
    QStringList stringList;

    for(auto& t : _->_visualisationChannels)
    {
        if(t.second->supports(valueType))
            stringList.append(t.first);
    }

    return stringList;
}

QStringList GraphModel::visualisationDescription(const QString& attributeName, const QStringList& channelNames) const
{
    QStringList descriptions;

    if(!u::contains(_->_attributes, attributeName))
        return descriptions;

    for(const auto& channelName : channelNames)
    {
        if(!u::contains(_->_visualisationChannels, channelName))
            return descriptions;

        auto attribute = attributeValueByName(attributeName);
        auto& channel = _->_visualisationChannels.at(channelName);

        if(!channel->supports(attribute.valueType()))
            descriptions.append(tr("This visualisation channel is not supported for the attribute type."));
        else
            descriptions.append(channel->description(attribute.elementType(), attribute.valueType()));
    }

    return descriptions;
}

void GraphModel::clearVisualisationInfos()
{
    _->_visualisationInfos.clear();
}

bool GraphModel::hasVisualisationInfo() const
{
    return !_->_visualisationInfos.empty();
}

const VisualisationInfo& GraphModel::visualisationInfoAtIndex(int index) const
{
    static VisualisationInfo nullVisualisationInfo;

    if(u::contains(_->_visualisationInfos, index))
        return _->_visualisationInfos.at(index);

    return nullVisualisationInfo;
}

QVariantMap GraphModel::visualisationDefaultParameters(ValueType valueType, const QString& channelName) const
{
    if(!u::contains(_->_visualisationChannels, channelName))
        return {};

    auto& channel = _->_visualisationChannels.at(channelName);

    return channel->defaultParameters(valueType);
}

std::vector<QString> GraphModel::attributeNames(ElementType elementType) const
{
    std::vector<QString> attributeNames;

    for(auto& attribute : _->_attributes)
    {
        if(Flags<ElementType>(elementType).test(attribute.second.elementType()))
            attributeNames.emplace_back(attribute.first);
    }

    return attributeNames;
}

Attribute& GraphModel::createAttribute(QString name)
{
    name = normalisedAttributeName(name);
    Attribute& attribute = _->_attributes[name];

    // If we're creating an attribute during the graph transform, it's
    // a dynamically created attribute rather than a persistent one,
    // so mark it as such
    if(_transformedGraphIsChanging)
        attribute.setFlag(AttributeFlag::Dynamic);

    return attribute;
}

void GraphModel::addAttributes(const std::map<QString, Attribute>& attributes)
{
    _->_attributes.insert(attributes.begin(), attributes.end());
}

void GraphModel::removeAttribute(const QString& name)
{
    if(u::contains(_->_attributes, name))
        _->_attributes.erase(name);
}

const Attribute* GraphModel::attributeByName(const QString& name) const
{
    auto attributeName = Attribute::parseAttributeName(name);

    if(!u::contains(_->_attributes, attributeName._name))
    {
        qDebug() << "WARNING: attribute unknown in attributeByName" << attributeName._name;
        return nullptr;
    }

    return &_->_attributes.at(attributeName._name);
}

bool GraphModel::attributeExists(const QString& name) const
{
    auto attributeName = Attribute::parseAttributeName(name);
    return u::contains(_->_attributes, attributeName._name);
}

bool GraphModel::attributeIsValid(const QString& name) const
{
    if(!attributeExists(name))
        return false;

    auto attributeName = Attribute::parseAttributeName(name);
    const auto* attribute = &_->_attributes.at(attributeName._name);

    bool attributeDisabled = _transformedGraphIsChanging &&
        attribute->testFlag(AttributeFlag::DisableDuringTransfom);

    return !attributeDisabled;
}

Attribute GraphModel::attributeValueByName(const QString& name) const
{
    auto attributeName = Attribute::parseAttributeName(name);

    if(!u::contains(_->_attributes, attributeName._name))
    {
        qDebug() << "WARNING: attribute unknown in attributeValueByName" << attributeName._name;
        return {};
    }

    auto attribute = _->_attributes.at(attributeName._name);

    if(!attributeName._parameter.isEmpty())
        attribute.setParameterValue(attributeName._parameter);

    if(attributeName._type != Attribute::EdgeNodeType::None)
    {
        return Attribute::edgeNodesAttribute(_->_graph,
            attribute, attributeName._type);
    }

    return attribute;
}

static void calculateAttributeRanges(const Graph* graph,
    std::map<QString, Attribute>& attributes)
{
    for(auto& attribute : make_value_wrapper(attributes))
        GraphModel::calculateAttributeRange(graph, attribute);
}

void GraphModel::initialiseAttributeRanges()
{
    calculateAttributeRanges(&mutableGraph(), _->_attributes);
}

static void findSharedAttributeValues(const Graph* graph,
    std::map<QString, Attribute>& attributes)
{
    for(auto& attribute : make_value_wrapper(attributes))
    {
        if(!attribute.testFlag(AttributeFlag::FindShared))
            continue;

        if(attribute.elementType() == ElementType::Node)
            attribute.updateSharedValuesForElements(graph->nodeIds());
        else if(attribute.elementType() == ElementType::Edge)
            attribute.updateSharedValuesForElements(graph->edgeIds());
    }
}

void GraphModel::initialiseUniqueAttributeValues()
{
    findSharedAttributeValues(&graph(), _->_attributes);
}

bool GraphModel::attributeNameIsValid(const QString& attributeName)
{
    if(attributeName.isEmpty())
        return false;

    auto attributeNameRegex = QRegularExpression(QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_ ]*$"));
    return attributeNameRegex.match(attributeName).hasMatch();
}

void GraphModel::calculateAttributeRange(const IGraph* graph, Attribute& attribute)
{
    if(!attribute.testFlag(AttributeFlag::AutoRange))
        return;

    if(attribute.elementType() == ElementType::Node)
        attribute.autoSetRangeForElements(graph->nodeIds());
    else if(attribute.elementType() == ElementType::Edge)
        attribute.autoSetRangeForElements(graph->edgeIds());
}

void GraphModel::clearHighlightedNodes()
{
    if(_->_highlightedNodeIds.empty())
        return;

    _->_highlightedNodeIds.clear();
    updateVisuals();
}

void GraphModel::highlightNodes(const NodeIdSet& nodeIds)
{
    if(_->_highlightedNodeIds.empty() && nodeIds.empty())
        return;

    _->_highlightedNodeIds = nodeIds;
    updateVisuals();
}

void GraphModel::enableVisualUpdates()
{
    _visualUpdatesEnabled = true;
    updateVisuals();
}

static float mappedSize(float min, float max, float user, float mapped)
{
    // The fraction of the mapped value that contributes to the final value
    const float mappedRange = 0.5f;

    auto normalised = u::normalise(min, max, user);
    auto out = (mapped * mappedRange) + (normalised * (1.0f - mappedRange));

    return min + (out * (max - min));
}

void GraphModel::updateVisuals()
{
    if(!_visualUpdatesEnabled)
        return;

    emit visualsWillChange();

    auto nodeColor      = u::pref("visuals/defaultNodeColor").value<QColor>();
    auto edgeColor      = u::pref("visuals/defaultEdgeColor").value<QColor>();
    auto multiColor     = u::pref("visuals/multiElementColor").value<QColor>();
    auto nodeSize       = u::pref("visuals/defaultNodeSize").toFloat();
    auto minNodeSize    = u::minPref("visuals/defaultNodeSize").toFloat();
    auto maxNodeSize    = u::maxPref("visuals/defaultNodeSize").toFloat();
    auto edgeSize       = u::pref("visuals/defaultEdgeSize").toFloat();
    auto minEdgeSize    = u::minPref("visuals/defaultEdgeSize").toFloat();
    auto maxEdgeSize    = u::maxPref("visuals/defaultEdgeSize").toFloat();
    auto meIndicators   = u::pref("visuals/showMultiElementIndicators").toBool();

    // Clear all edge flags as we can't know what to
    // change unless we have the previous state to hand
    for(auto edgeId : graph().edgeIds())
        _->_edgeVisuals[edgeId]._state.reset(VisualFlags::Selected, VisualFlags::Unhighlighted);

    for(auto nodeId : graph().nodeIds())
    {
        // Size
        if(_->_mappedNodeVisuals[nodeId]._size >= 0.0f)
        {
            _->_nodeVisuals[nodeId]._size = mappedSize(minNodeSize, maxNodeSize, nodeSize,
                                         _->_mappedNodeVisuals[nodeId]._size);
        }
        else
            _->_nodeVisuals[nodeId]._size = nodeSize;

        // Color
        if(!_->_mappedNodeVisuals[nodeId]._outerColor.isValid())
            _->_nodeVisuals[nodeId]._outerColor = nodeColor;
        else
            _->_nodeVisuals[nodeId]._outerColor = _->_mappedNodeVisuals[nodeId]._outerColor;

        _->_nodeVisuals[nodeId]._innerColor = !meIndicators || graph().typeOf(nodeId) == MultiElementType::Not ?
                    _->_nodeVisuals[nodeId]._outerColor : multiColor;

        // Text
        if(!_->_mappedNodeVisuals[nodeId]._text.isEmpty())
            _->_nodeVisuals[nodeId]._text = _->_mappedNodeVisuals[nodeId]._text;
        else
            _->_nodeVisuals[nodeId]._text = nodeName(nodeId);

        auto nodeIsSelected = u::contains(_->_selectedNodeIds, nodeId);

        _->_nodeVisuals[nodeId]._state.setState(VisualFlags::Selected, nodeIsSelected);

        if(nodeIsSelected)
        {
            for(auto edgeId : graph().edgeIdsForNodeId(nodeId))
                _->_edgeVisuals[edgeId]._state.setState(VisualFlags::Selected, nodeIsSelected);
        }

        auto isNotFound = !_->_foundNodeIds.empty() && !u::contains(_->_foundNodeIds, nodeId);
        auto isNotHighlighted = !_->_highlightedNodeIds.empty() && nodeIsSelected &&
            !u::contains(_->_highlightedNodeIds, nodeId);

        auto nodeUnhighlighted = (isNotFound && _->_nodesMaskActive) || isNotHighlighted;

        _->_nodeVisuals[nodeId]._state.setState(VisualFlags::Unhighlighted, nodeUnhighlighted);

        if(nodeUnhighlighted)
        {
            for(auto edgeId : graph().edgeIdsForNodeId(nodeId))
                _->_edgeVisuals[edgeId]._state.set(VisualFlags::Unhighlighted);
        }
    }

    for(auto edgeId : graph().edgeIds())
    {
        // Size
        if(_->_mappedEdgeVisuals[edgeId]._size >= 0.0f)
        {
            _->_edgeVisuals[edgeId]._size = mappedSize(minEdgeSize, maxEdgeSize, edgeSize,
                                         _->_mappedEdgeVisuals[edgeId]._size);
        }
        else
            _->_edgeVisuals[edgeId]._size = edgeSize;

        // Restrict edgeSize to be no larger than the source or target size
        const auto& edge = graph().edgeById(edgeId);
        auto minEdgeNodesSize = std::min(_->_nodeVisuals[edge.sourceId()]._size,
                                         _->_nodeVisuals[edge.targetId()]._size);
        _->_edgeVisuals[edgeId]._size = std::min(_->_edgeVisuals[edgeId]._size, minEdgeNodesSize);

        // Color
        if(!_->_mappedEdgeVisuals[edgeId]._outerColor.isValid())
            _->_edgeVisuals[edgeId]._outerColor = edgeColor;
        else
            _->_edgeVisuals[edgeId]._outerColor = _->_mappedEdgeVisuals[edgeId]._outerColor;

        _->_edgeVisuals[edgeId]._innerColor = !meIndicators || graph().typeOf(edgeId) == MultiElementType::Not ?
            _->_edgeVisuals[edgeId]._outerColor : multiColor;

        // Text
        if(!_->_mappedEdgeVisuals[edgeId]._text.isEmpty())
            _->_edgeVisuals[edgeId]._text = _->_mappedEdgeVisuals[edgeId]._text;
        else
            _->_edgeVisuals[edgeId]._text.clear();
    }

    emit visualsChanged();
}

void GraphModel::onSelectionChanged(const SelectionManager* selectionManager)
{
    _->_selectedNodeIds = selectionManager->selectedNodes();
    _->_nodesMaskActive = selectionManager->nodesMaskActive();
    clearHighlightedNodes();
    updateVisuals();
}

void GraphModel::onFoundNodeIdsChanged(const SearchManager* searchManager)
{
    _->_foundNodeIds = searchManager->foundNodeIds();
    updateVisuals();
}

void GraphModel::onPreferenceChanged(const QString& name, const QVariant&)
{
    if(!name.startsWith(QStringLiteral("visuals")))
        return;

    updateVisuals();
}

void GraphModel::onMutableGraphChanged(const Graph* graph)
{
    calculateAttributeRanges(graph, _->_attributes);
}

void GraphModel::onTransformedGraphWillChange(const Graph*)
{
    // Store previous dynamic attributes for comparison
    _->_previousDynamicAttributeNames.clear();

    for(auto& name : u::keysFor(_->_attributes))
        _->_previousDynamicAttributeNames.emplace_back(name);

    removeDynamicAttributes();

    _transformedGraphIsChanging = true;
}

void GraphModel::onTransformedGraphChanged(const Graph* graph)
{
    _transformedGraphIsChanging = false;

    findSharedAttributeValues(graph, _->_attributes);

    // Compare with previous Dynamic attributes
    auto removedAttributeNames = u::setDifference(_->_previousDynamicAttributeNames, u::keysFor(_->_attributes));
    auto addedAttributeNames = u::setDifference(u::keysFor(_->_attributes), _->_previousDynamicAttributeNames);

    emit attributesChanged(u::toQStringList(addedAttributeNames), u::toQStringList(removedAttributeNames));
}
