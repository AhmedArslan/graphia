#include "correlationplugin.h"

#include "correlation.h"
#include "correlationplotitem.h"
#include "graphsizeestimateplotitem.h"

#include "shared/utils/threadpool.h"
#include "shared/utils/iterator_range.h"
#include "shared/utils/container.h"
#include "shared/utils/random.h"
#include "shared/utils/string.h"

#include "shared/attributes/iattribute.h"

#include "shared/ui/visualisations/ielementvisual.h"

#include "shared/loading/xlsxtabulardataparser.h"

#include <json_helper.h>

CorrelationPluginInstance::CorrelationPluginInstance()
{
    connect(this, SIGNAL(loadSuccess()), this, SLOT(onLoadSuccess()));
    connect(this, SIGNAL(selectionChanged(const ISelectionManager*)),
            this, SLOT(onSelectionChanged(const ISelectionManager*)), Qt::DirectConnection);
    connect(this, SIGNAL(visualsChanged()), this, SIGNAL(nodeColorsChanged()));
}

void CorrelationPluginInstance::initialise(const IPlugin* plugin, IDocument* document,
                                           const IParserThread* parserThread)
{
    BasePluginInstance::initialise(plugin, document, parserThread);

    auto graphModel = document->graphModel();
    _userNodeData.initialise(graphModel->mutableGraph());

    if(_transpose)
    {
        // Don't include data columns in the table model when transposing as this is likely to
        // result in a very large number of columns in the table model, which hurt performance
        _nodeAttributeTableModel.initialise(document, &_userNodeData);
    }
    else
        _nodeAttributeTableModel.initialise(document, &_userNodeData, &_dataColumnNames, &_data);

    _correlationValues = std::make_unique<EdgeArray<double>>(graphModel->mutableGraph());
}

bool CorrelationPluginInstance::loadUserData(const TabularData& tabularData,
    size_t firstDataColumn, size_t firstDataRow, IParser& parser)
{
    if(firstDataColumn == 0 || firstDataRow == 0)
    {
        qDebug() << "tabularData has no row or column names!";
        return false;
    }

    parser.setProgress(-1);

    uint64_t numDataPoints = static_cast<uint64_t>(tabularData.numColumns()) * tabularData.numRows();

    for(size_t rowIndex = 0; rowIndex < tabularData.numRows(); rowIndex++)
    {
        for(size_t columnIndex = 0; columnIndex < tabularData.numColumns(); columnIndex++)
        {
            if(parser.cancelled())
                return false;

            uint64_t rowOffset = static_cast<uint64_t>(rowIndex) * tabularData.numColumns();
            uint64_t dataPoint = columnIndex + rowOffset;
            parser.setProgress(static_cast<int>((dataPoint * 100) / numDataPoints));

            const auto& value = tabularData.valueAt(columnIndex, rowIndex);

            size_t dataColumnIndex = columnIndex - firstDataColumn;
            size_t dataRowIndex = rowIndex - firstDataRow;
            bool isRowInDataRect = firstDataRow <= rowIndex;
            bool isColumnInDataRect = firstDataColumn <= columnIndex;

            if((isColumnInDataRect && dataColumnIndex >= _numColumns) ||
                (isRowInDataRect && dataRowIndex >= _numRows))
            {
                qDebug() << QString("WARNING: Attempting to set data at coordinate (%1, %2) in "
                                    "dataRect of dimensions (%3, %4)")
                            .arg(dataColumnIndex)
                            .arg(dataRowIndex)
                            .arg(_numColumns)
                            .arg(_numRows);
                continue;
            }

            if(rowIndex == 0)
            {
                if(!isColumnInDataRect)
                    _userNodeData.add(value);
                else
                    setDataColumnName(dataColumnIndex, value);
            }
            else if(!isRowInDataRect)
            {
                if(columnIndex == 0)
                    _userColumnData.add(value);
                else if(isColumnInDataRect)
                    _userColumnData.setValue(dataColumnIndex, tabularData.valueAt(0, rowIndex), value);
            }
            else if(isColumnInDataRect)
            {
                double transformedValue = 0.0;

                if(!value.isEmpty())
                {
                    bool success = false;
                    transformedValue = value.toDouble(&success);
                    Q_ASSERT(success);
                }
                else
                {
                    transformedValue = CorrelationFileParser::imputeValue(_missingDataType, _missingDataReplacementValue,
                        tabularData, firstDataColumn, firstDataRow, columnIndex, rowIndex);
                }

                transformedValue = CorrelationFileParser::scaleValue(_scalingType, transformedValue);

                setData(dataColumnIndex, dataRowIndex, transformedValue);
            }
            else // Not in data rect, not first row, put in to the userNodeData
                _userNodeData.setValue(dataRowIndex, tabularData.valueAt(columnIndex, 0), value);
        }
    }

    parser.setProgress(-1);

    return true;
}

void CorrelationPluginInstance::normalise(IParser* parser)
{
    CorrelationFileParser::normalise(_normaliseType, _dataRows, parser);

    // Normalising obviously changes all the values in _dataRows, so we
    // must sync _data up so that it matches
    _data.clear();
    for(const auto& dataRow : _dataRows)
        _data.insert(_data.end(), dataRow.begin(), dataRow.end());
}

void CorrelationPluginInstance::finishDataRows()
{
    for(size_t row = 0; row < _numRows; row++)
        finishDataRow(row);
}

void CorrelationPluginInstance::createAttributes()
{
    graphModel()->createAttribute(tr("Mean Data Value"))
            .setFloatValueFn([this](NodeId nodeId) { return dataRowForNodeId(nodeId).mean(); })
            .setFlag(AttributeFlag::AutoRange)
            .setDescription(tr("The Mean Data Value is the mean of the values associated "
                               "with the node."));

    graphModel()->createAttribute(tr("Minimum Data Value"))
            .setFloatValueFn([this](NodeId nodeId) { return dataRowForNodeId(nodeId).minValue(); })
            .setFlag(AttributeFlag::AutoRange)
            .setDescription(tr("The Minimum Data Value is the minimum value associated "
                               "with the node."));

    graphModel()->createAttribute(tr("Maximum Data Value"))
            .setFloatValueFn([this](NodeId nodeId) { return dataRowForNodeId(nodeId).maxValue(); })
            .setFlag(AttributeFlag::AutoRange)
            .setDescription(tr("The Maximum Data Value is the maximum value associated "
                               "with the node."));

    graphModel()->createAttribute(tr("Variance"))
            .setFloatValueFn([this](NodeId nodeId) { return dataRowForNodeId(nodeId).variance(); })
            .setFlag(AttributeFlag::AutoRange)
            .setDescription(tr(R"(The <a href="https://kajeka.com/graphia/variance">Variance</a> )"
                               "is a measure of the spread of the values associated "
                               "with the node. It is defined as ∑(<i>x</i>-µ)², where <i>x</i> is the value "
                               "and µ is the mean."));

    graphModel()->createAttribute(tr("Standard Deviation"))
            .setFloatValueFn([this](NodeId nodeId) { return dataRowForNodeId(nodeId).stddev(); })
            .setFlag(AttributeFlag::AutoRange)
            .setDescription(tr(R"(The <a href="https://kajeka.com/graphia/stddev">)"
                               "Standard Deviation</a> is a measure of the spread of the values associated "
                               "with the node. It is defined as √∑(<i>x</i>-µ)², where <i>x</i> is the value "
                               "and µ is the mean."));

    graphModel()->createAttribute(tr("Coefficient of Variation"))
            .setFloatValueFn([this](NodeId nodeId) { return dataRowForNodeId(nodeId).coefVar(); })
            .setValueMissingFn([this](NodeId nodeId) { return std::isnan(dataRowForNodeId(nodeId).coefVar()); })
            .setFlag(AttributeFlag::AutoRange)
            .setDescription(tr(R"(The <a href="https://kajeka.com/graphia/coef_variation">)"
                               "Coefficient of Variation</a> "
                               "is a measure of the spread of the values associated "
                               "with the node. It is defined as the standard deviation "
                               "divided by the mean."));

    auto correlation = Correlation::create(static_cast<CorrelationType>(_correlationType));
    graphModel()->createAttribute(correlation->attributeName())
            .setFloatValueFn([this](EdgeId edgeId) { return _correlationValues->get(edgeId); })
            .setFlag(AttributeFlag::AutoRange)
            .setDescription(correlation->attributeDescription());
}

void CorrelationPluginInstance::setHighlightedRows(const QVector<int>& highlightedRows)
{
    if(_highlightedRows.isEmpty() && highlightedRows.isEmpty())
        return;

    _highlightedRows = highlightedRows;

    NodeIdSet highlightedNodeIds;
    for(auto row : highlightedRows)
    {
        auto nodeId = _userNodeData.elementIdForRowIndex(static_cast<size_t>(row));
        highlightedNodeIds.insert(nodeId);
    }

    document()->highlightNodes(highlightedNodeIds);

    emit highlightedRowsChanged();
}

std::vector<CorrelationEdge> CorrelationPluginInstance::correlation(double minimumThreshold, IParser& parser)
{
    auto correlation = Correlation::create(static_cast<CorrelationType>(_correlationType));
    return correlation->process(_dataRows, minimumThreshold, &parser);
}

bool CorrelationPluginInstance::createEdges(const std::vector<CorrelationEdge>& edges, IParser& parser)
{
    parser.setProgress(-1);
    for(auto edgeIt = edges.begin(); edgeIt != edges.end(); ++edgeIt)
    {
        if(parser.cancelled())
            return false;

        parser.setProgress(std::distance(edges.begin(), edgeIt) * 100 / static_cast<int>(edges.size()));

        auto& edge = *edgeIt;
        auto edgeId = graphModel()->mutableGraph().addEdge(edge._source, edge._target);
        _correlationValues->set(edgeId, edge._r);
    }

    return true;
}

void CorrelationPluginInstance::setDimensions(size_t numColumns, size_t numRows)
{
    Q_ASSERT(_dataColumnNames.empty());
    Q_ASSERT(_userColumnData.empty());
    Q_ASSERT(_userNodeData.empty());

    _numColumns = numColumns;
    _numRows = numRows;

    _dataColumnNames.resize(numColumns);
    _data.resize(numColumns * numRows);
}

void CorrelationPluginInstance::setDataColumnName(size_t column, const QString& name)
{
    Q_ASSERT(column < _numColumns);
    _dataColumnNames.at(column) = name;
}

void CorrelationPluginInstance::setData(size_t column, size_t row, double value)
{
    auto index = (row * _numColumns) + column;
    Q_ASSERT(index < _data.size());
    _data.at(index) = value;
}

void CorrelationPluginInstance::finishDataRow(size_t row)
{
    Q_ASSERT(row < _numRows);

    auto nodeId = graphModel()->mutableGraph().addNode();
    auto computeCost = static_cast<int>(_numRows - row + 1);

    _dataRows.emplace_back(_data, row, _numColumns, nodeId, computeCost);
    _userNodeData.setElementIdForRowIndex(nodeId, row);

    auto nodeName = _userNodeData.valueBy(nodeId, _userNodeData.firstUserDataVectorName()).toString();
    graphModel()->setNodeName(nodeId, nodeName);
}

QStringList CorrelationPluginInstance::columnAnnotationNames() const
{
    QStringList list;
    list.reserve(static_cast<int>(_columnAnnotations.size()));

    for(const auto& columnAnnotation : _columnAnnotations)
        list.append(columnAnnotation.name());

    return list;
}

void CorrelationPluginInstance::onLoadSuccess()
{
    _userNodeData.exposeAsAttributes(*graphModel());
    _nodeAttributeTableModel.updateRoleNames();
    buildColumnAnnotations();
}

QVector<double> CorrelationPluginInstance::rawData()
{
    return QVector<double>::fromStdVector(_data);
}

void CorrelationPluginInstance::buildColumnAnnotations()
{
    _columnAnnotations.reserve(_userColumnData.numUserDataVectors());

    for(const auto& [name, values] : _userColumnData)
    {
        auto numValues = values.numValues();
        auto numUniqueValues = values.numUniqueValues();

        // If the number of unique values is more than a half of the total
        // number of values, skip it, since a large number of unique values
        // potentially causes performance problems, and it's probably not a
        // useful annotation in the first place
        if(numUniqueValues * 2 > numValues)
            continue;

        _columnAnnotations.emplace_back(name, values.begin(), values.end());
    }

    emit columnAnnotationNamesChanged();
}

const CorrelationDataRow& CorrelationPluginInstance::dataRowForNodeId(NodeId nodeId) const
{
    return _dataRows.at(_userNodeData.rowIndexFor(nodeId));
}

void CorrelationPluginInstance::onSelectionChanged(const ISelectionManager*)
{
    _nodeAttributeTableModel.onSelectionChanged();
}

std::unique_ptr<IParser> CorrelationPluginInstance::parserForUrlTypeName(const QString& urlTypeName)
{
    std::vector<QString> urlTypes =
    {
        QStringLiteral("CorrelationCSV"),
        QStringLiteral("CorrelationTSV"),
        QStringLiteral("CorrelationXLSX")
    };

    if(u::contains(urlTypes, urlTypeName))
        return std::make_unique<CorrelationFileParser>(this, urlTypeName, _tabularData, _dataRect);

    return nullptr;
}

void CorrelationPluginInstance::applyParameter(const QString& name, const QVariant& value)
{
    if(name == QLatin1String("minimumCorrelation"))
        _minimumCorrelationValue = value.toDouble();
    else if(name == QLatin1String("initialThreshold"))
        _initialCorrelationThreshold = value.toDouble();
    else if(name == QLatin1String("transpose"))
        _transpose = (value == QLatin1String("true"));
    else if(name == QLatin1String("correlationType"))
        _correlationType = static_cast<CorrelationType>(value.toInt());
    else if(name == QLatin1String("scaling"))
        _scalingType = static_cast<ScalingType>(value.toInt());
    else if(name == QLatin1String("normalise"))
        _normaliseType = static_cast<NormaliseType>(value.toInt());
    else if(name == QLatin1String("missingDataType"))
        _missingDataType = static_cast<MissingDataType>(value.toInt());
    else if(name == QLatin1String("missingDataValue"))
        _missingDataReplacementValue = value.toDouble();
    else if(name == QLatin1String("dataFrame"))
        _dataRect = value.toRect();
    else if(name == QLatin1String("clusteringType"))
        _clusteringType = static_cast<ClusteringType>(value.toInt());
    else if(name == QLatin1String("edgeReductionType"))
        _edgeReductionType = static_cast<EdgeReductionType>(value.toInt());
    else if(name == QLatin1String("data") && value.canConvert<std::shared_ptr<TabularData>>())
        _tabularData = std::move(*value.value<std::shared_ptr<TabularData>>());
}

QStringList CorrelationPluginInstance::defaultTransforms() const
{
    auto correlation = Correlation::create(static_cast<CorrelationType>(_correlationType));

    QStringList defaultTransforms =
    {
        QString(R"("Remove Edges" where $"%1" < %2)")
            .arg(correlation->attributeName())
            .arg(_initialCorrelationThreshold),
        R"([pinned] "Remove Components" where $"Component Size" <= 1)"
    };

    if(_edgeReductionType == EdgeReductionType::KNN)
    {
        defaultTransforms.append(QStringLiteral(R"("k-NN" using $"%1")")
            .arg(correlation->attributeName()));
    }

    if(_clusteringType == ClusteringType::MCL)
        defaultTransforms.append(QStringLiteral(R"("MCL Cluster")"));

    return defaultTransforms;
}

QStringList CorrelationPluginInstance::defaultVisualisations() const
{
    if(_clusteringType == ClusteringType::MCL)
        return { R"("MCL Cluster" "Colour")" };

    return {};
}

size_t CorrelationPluginInstance::numColumns() const
{
    return _numColumns;
}

double CorrelationPluginInstance::dataAt(int row, int column) const
{
    return _data.at((row * _numColumns) + column);
}

QString CorrelationPluginInstance::rowName(int row) const
{
    const auto& [name, firstColumn] = *_userNodeData.begin();
    return firstColumn.get(row);
}

QString CorrelationPluginInstance::columnName(int column) const
{
    return _dataColumnNames.at(static_cast<size_t>(column));
}

QColor CorrelationPluginInstance::nodeColorForRow(int row) const
{
    auto nodeId = _userNodeData.elementIdForRowIndex(row);

    if(nodeId.isNull())
        return {};

    return graphModel()->nodeVisual(nodeId).outerColor();
}

QByteArray CorrelationPluginInstance::save(IMutableGraph& graph, Progressable& progressable) const
{
    json jsonObject;

    jsonObject["numColumns"] = static_cast<int>(_numColumns);
    jsonObject["numRows"] = static_cast<int>(_numRows);
    jsonObject["userNodeData"] = _userNodeData.save(graph, progressable);
    jsonObject["userColumnData"] =_userColumnData.save(progressable);
    jsonObject["dataColumnNames"] = jsonArrayFrom(_dataColumnNames, &progressable);

    graph.setPhase(QObject::tr("Data"));
    jsonObject["data"] = jsonArrayFrom(_data, &progressable);

    graph.setPhase(QObject::tr("Correlation Values"));
    jsonObject["correlationValues"] = jsonArrayFrom(*_correlationValues);

    jsonObject["minimumCorrelationValue"] = _minimumCorrelationValue;
    jsonObject["transpose"] = _transpose;
    jsonObject["correlationType"] = static_cast<int>(_correlationType);
    jsonObject["scaling"] = static_cast<int>(_scalingType);
    jsonObject["normalisation"] = static_cast<int>(_normaliseType);
    jsonObject["missingDataType"] = static_cast<int>(_missingDataType);
    jsonObject["missingDataReplacementValue"] = _missingDataReplacementValue;

    return QByteArray::fromStdString(jsonObject.dump());
}

bool CorrelationPluginInstance::load(const QByteArray& data, int dataVersion, IMutableGraph& graph,
                                     IParser& parser)
{
    if(dataVersion > plugin()->dataVersion())
        return false;

    json jsonObject = parseJsonFrom(data, &parser);

    if(parser.cancelled())
        return false;

    if(jsonObject.is_null() || !jsonObject.is_object())
        return false;

    if(!u::contains(jsonObject, "numColumns") || !u::contains(jsonObject, "numRows"))
        return false;

    _numColumns = static_cast<size_t>(jsonObject["numColumns"].get<int>());
    _numRows = static_cast<size_t>(jsonObject["numRows"].get<int>());

    if(!u::contains(jsonObject, "userNodeData") || !u::contains(jsonObject, "userColumnData"))
        return false;

    if(!_userNodeData.load(jsonObject["userNodeData"], parser))
        return false;

    if(!_userColumnData.load(jsonObject["userColumnData"], parser))
        return false;

    parser.setProgress(-1);

    if(!u::contains(jsonObject, "dataColumnNames"))
        return false;

    for(const auto& dataColumnName : jsonObject["dataColumnNames"])
        _dataColumnNames.emplace_back(QString::fromStdString(dataColumnName));

    uint64_t i = 0;

    if(!u::contains(jsonObject, "data"))
        return false;

    graph.setPhase(QObject::tr("Data"));
    const auto& jsonData = jsonObject["data"];
    for(const auto& value : jsonData)
    {
        _data.emplace_back(value);
        parser.setProgress(static_cast<int>((i++ * 100) / jsonData.size()));
    }

    parser.setProgress(-1);

    for(size_t row = 0; row < _numRows; row++)
    {
        auto nodeId = _userNodeData.elementIdForRowIndex(row);
        _dataRows.emplace_back(_data, row, _numColumns, nodeId);

        parser.setProgress(static_cast<int>((row * 100) / _numRows));
    }

    parser.setProgress(-1);

    const char* correlationValuesKey =
        dataVersion >= 2 ? "correlationValues" : "pearsonValues";

    if(!u::contains(jsonObject, correlationValuesKey))
        return false;

    const auto& jsonCorrelationValues = jsonObject[correlationValuesKey];
    graph.setPhase(QObject::tr("Correlation Values"));
    i = 0;
    for(const auto& correlationValue : jsonCorrelationValues)
    {
        if(graph.containsEdgeId(i))
            _correlationValues->set(i, correlationValue);

        parser.setProgress(static_cast<int>((i++ * 100) / jsonCorrelationValues.size()));
    }

    parser.setProgress(-1);

    if(!u::containsAllOf(jsonObject, {"minimumCorrelationValue", "transpose", "scaling",
        "normalisation", "missingDataType", "missingDataReplacementValue"}))
    {
        return false;
    }

    _minimumCorrelationValue = jsonObject["minimumCorrelationValue"];
    _transpose = jsonObject["transpose"];
    _scalingType = static_cast<ScalingType>(jsonObject["scaling"]);
    _normaliseType = static_cast<NormaliseType>(jsonObject["normalisation"]);
    _missingDataType = static_cast<MissingDataType>(jsonObject["missingDataType"]);
    _missingDataReplacementValue = jsonObject["missingDataReplacementValue"];

    if(dataVersion >= 2)
    {
        if(!u::contains(jsonObject, "correlationType"))
            return false;

        _correlationType = static_cast<CorrelationType>(jsonObject["correlationType"]);
    }

    createAttributes();

    return true;
}

CorrelationPlugin::CorrelationPlugin()
{
    registerUrlType(QStringLiteral("CorrelationCSV"), QObject::tr("Correlation CSV File"), QObject::tr("Correlation CSV Files"), {"csv"});
    registerUrlType(QStringLiteral("CorrelationTSV"), QObject::tr("Correlation TSV File"), QObject::tr("Correlation TSV Files"), {"tsv"});
    registerUrlType(QStringLiteral("CorrelationXLSX"), QObject::tr("Correlation Excel File"), QObject::tr("Correlation Excel Files"), {"xlsx"});

    qmlRegisterType<CorrelationPluginInstance>("com.kajeka", 1, 0, "CorrelationPluginInstance");
    qmlRegisterType<CorrelationPlotItem>("com.kajeka", 1, 0, "CorrelationPlot");
    qmlRegisterType<GraphSizeEstimatePlotItem>("com.kajeka", 1, 0, "GraphSizeEstimatePlot");
    qmlRegisterType<TabularDataParser>("com.kajeka", 1, 0, "TabularDataParser");
    qmlRegisterType<DataRectTableModel>("com.kajeka", 1, 0, "DataRectTableModel");
}

static QString contentIdentityOf(const QUrl& url)
{
    if(XlsxTabularDataParser::canLoad(url))
        return QStringLiteral("CorrelationXLSX");

    QString identity;

    std::ifstream file(url.toLocalFile().toStdString());
    std::string line;

    if(file && u::getline(file, line))
    {
        size_t numCommas = 0;
        size_t numTabs = 0;
        bool inQuotes = false;

        for(auto character : line)
        {
            switch(character)
            {
            case '"': inQuotes = !inQuotes; break;
            case ',': if(!inQuotes) { numCommas++; } break;
            case '\t': if(!inQuotes) { numTabs++; } break;
            default: break;
            }
        }

        if(numTabs > numCommas)
            identity = QStringLiteral("CorrelationTSV");
        else if(numCommas > numTabs)
            identity = QStringLiteral("CorrelationCSV");
    }

    return identity;
}

QStringList CorrelationPlugin::identifyUrl(const QUrl& url) const
{
    auto urlTypes = identifyByExtension(url);

    if(urlTypes.isEmpty() || contentIdentityOf(url) != urlTypes.first())
        return {};

    return urlTypes;
}

QString CorrelationPlugin::failureReason(const QUrl& url) const
{
    auto urlTypes = identifyByExtension(url);

    if(urlTypes.isEmpty())
        return {};

    auto extensionIdentity = urlTypes.first();
    auto contentIdentity = contentIdentityOf(url);

    if(extensionIdentity != contentIdentity)
    {
        return tr("%1 has an extension that indicates it is a '%2', "
            "however its content resembles a '%3'.").arg(url.fileName(),
            individualDescriptionForUrlTypeName(extensionIdentity),
            individualDescriptionForUrlTypeName(contentIdentity));
    }

    return {};
}
