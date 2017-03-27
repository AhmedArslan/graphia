#include "correlationplotitem.h"

#include <QDesktopServices>

#include <random>

CorrelationPlotItem::CorrelationPlotItem(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    _customPlot.setOpenGl(true);
    _customPlot.addLayer("textLayer");

    _textLayer = _customPlot.layer("textLayer");
    _textLayer->setMode(QCPLayer::LayerMode::lmBuffered);

    QFont defaultFont10Pt;
    defaultFont10Pt.setPointSize(10);

    _defaultFont9Pt.setPointSize(9);

    _hoverLabel = new QCPItemText(&_customPlot);
    _hoverLabel->setLayer(_textLayer);
    _hoverLabel->setPositionAlignment(Qt::AlignVCenter|Qt::AlignLeft);
    _hoverLabel->setFont(defaultFont10Pt);
    _hoverLabel->setPen(QPen(Qt::black));
    _hoverLabel->setBrush(QBrush(Qt::white));
    _hoverLabel->setPadding(QMargins(3, 3, 3, 3));
    _hoverLabel->setClipToAxisRect(false);
    _hoverLabel->setVisible(false);

    _hoverColorRect = new QCPItemRect(&_customPlot);
    _hoverColorRect->setLayer(_textLayer);
    _hoverColorRect->topLeft->setParentAnchor(_hoverLabel->topRight);
    _hoverColorRect->setClipToAxisRect(false);
    _hoverColorRect->setVisible(false);

    _itemTracer = new QCPItemTracer(&_customPlot);
    _itemTracer->setBrush(QBrush(Qt::white));
    _itemTracer->setLayer(_textLayer);
    _itemTracer->setInterpolating(false);
    _itemTracer->setVisible(true);
    _itemTracer->setStyle(QCPItemTracer::TracerStyle::tsCircle);
    _itemTracer->setClipToAxisRect(false);

    setFlag(QQuickItem::ItemHasContents, true);

    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::AllButtons);

    connect(this, &QQuickPaintedItem::widthChanged, this, &CorrelationPlotItem::updateCustomPlotSize);
    connect(this, &QQuickPaintedItem::heightChanged, this, &CorrelationPlotItem::updateCustomPlotSize);
    connect(&_customPlot, &QCustomPlot::afterReplot, this, &CorrelationPlotItem::onCustomReplot);
}

void CorrelationPlotItem::refresh()
{
    updateCustomPlotSize();
    buildPlot();
    _customPlot.replot();
}

void CorrelationPlotItem::paint(QPainter* painter)
{
    QPixmap    picture(boundingRect().size().toSize());
    QCPPainter qcpPainter(&picture);

    _customPlot.toPainter(&qcpPainter);

    painter->drawPixmap(QPoint(), picture);
}

void CorrelationPlotItem::mousePressEvent(QMouseEvent* event)
{
    routeMouseEvent(event);
}

void CorrelationPlotItem::mouseReleaseEvent(QMouseEvent* event)
{
    routeMouseEvent(event);
    if(event->button() == Qt::RightButton)
        emit rightClick();
}

void CorrelationPlotItem::mouseMoveEvent(QMouseEvent* event)
{
    routeMouseEvent(event);
}

void CorrelationPlotItem::hoverMoveEvent(QHoverEvent* event)
{
    _hoverPoint = event->posF();

    auto* currentPlottable = _customPlot.plottableAt(event->posF());
    if(_hoverPlottable != currentPlottable)
    {
        _hoverPlottable = currentPlottable;
        hideTooltip();
    }

    if(_hoverPlottable != nullptr)
        showTooltip();
}

void CorrelationPlotItem::hoverLeaveEvent(QHoverEvent*)
{
    hideTooltip();
}

void CorrelationPlotItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    routeMouseEvent(event);
}

void CorrelationPlotItem::wheelEvent(QWheelEvent* event)
{
    routeWheelEvent(event);
}

void CorrelationPlotItem::buildPlot()
{
    // If the legend is not cleared first this will cause a slowdown
    // when removing a large number of graphs
    _customPlot.legend->clear();
    _customPlot.clearGraphs();

    while(_customPlot.plotLayout()->rowCount() > 1)
    {
        _customPlot.plotLayout()->removeAt(_customPlot.plotLayout()->rowColToIndex(1, 0));
        _customPlot.plotLayout()->simplify();
    }

    if(_selectedRows.length() > MAX_SELECTED_ROWS_BEFORE_MEAN)
        populateMeanAveragePlot();
    else
        populateRawPlot();

    QSharedPointer<QCPAxisTickerText> categoryTicker(new QCPAxisTickerText);
    _customPlot.xAxis->setTicker(categoryTicker);
    _customPlot.xAxis->setTickLabelRotation(90);

    bool columnNamesSuppressed = false;

    if(_showColumnNames)
    {
        if(_elideLabelWidth > 0)
        {
            QFontMetrics metrics(_defaultFont9Pt);
            int column = 0;

            for(auto& labelName : _labelNames)
                categoryTicker->addTick(column++, metrics.elidedText(labelName, Qt::ElideRight, _elideLabelWidth));
        }
        else
        {
            auto* text = new QCPTextElement(&_customPlot);
            text->setLayer(_textLayer);
            text->setTextFlags(Qt::AlignLeft);
            text->setFont(_defaultFont9Pt);
            text->setTextColor(Qt::gray);
            text->setText(tr("Resize To Expose Column Names"));
            text->setVisible(true);

            _customPlot.plotLayout()->insertRow(1);
            _customPlot.plotLayout()->addElement(1, 0, text);

            columnNamesSuppressed = true;
        }
    }

    if(columnNamesSuppressed && _customPlot.plotLayout()->rowCount() > 1)
    {
        auto margins = _customPlot.axisRect()->margins();
        margins.setBottom(0);
        _customPlot.axisRect()->setAutoMargins(QCP::MarginSide::msLeft|
                                               QCP::MarginSide::msRight|
                                               QCP::MarginSide::msTop);
        _customPlot.axisRect()->setMargins(margins);
    }
    else
        _customPlot.axisRect()->setAutoMargins(QCP::MarginSide::msAll);
}

void CorrelationPlotItem::populateMeanAveragePlot()
{
    double maxX = static_cast<double>(_columnCount);
    double maxY = 0.0;

    std::random_device randomDevice;
    std::mt19937 mTwister(randomDevice());
    std::uniform_int_distribution<> randomColorDist(0, 255);

    auto* graph = _customPlot.addGraph();
    mTwister.seed(static_cast<unsigned int>(_selectedRows.count()));
    QColor randomColor = QColor::fromHsl(randomColorDist(mTwister), 210, 130);
    graph->setPen(QPen(randomColor));
    graph->setName(tr("Mean average of selection"));

    // Use Average Calculation
    QVector<double> yDataAvg;
    QVector<double> xData;

    for(size_t col = 0; col < _columnCount; col++)
    {
        double runningTotal = 0.0;
        for(auto row : _selectedRows)
        {
            auto index = (row * _columnCount) + col;
            runningTotal += _data[static_cast<int>(index)];
        }
        xData.append(static_cast<double>(col));
        yDataAvg.append(runningTotal / _selectedRows.length());

        maxY = std::max(maxY, yDataAvg.back());
    }
    graph->setData(xData, yDataAvg, true);

    auto* plotModeTextElement = new QCPTextElement(&_customPlot);
    plotModeTextElement->setLayer(_textLayer);
    plotModeTextElement->setTextFlags(Qt::AlignLeft);
    plotModeTextElement->setFont(_defaultFont9Pt);
    plotModeTextElement->setTextColor(Qt::gray);
    plotModeTextElement->setText(
        QString(tr("*Mean average plot of %1 rows (maximum row count for individual plots is %2)"))
                .arg(_selectedRows.length())
                .arg(MAX_SELECTED_ROWS_BEFORE_MEAN));
    plotModeTextElement->setVisible(true);

    _customPlot.plotLayout()->insertRow(1);
    _customPlot.plotLayout()->addElement(1, 0, plotModeTextElement);

    _customPlot.xAxis->setRange(0, maxX);
    _customPlot.yAxis->setRange(0, maxY);
}

void CorrelationPlotItem::populateRawPlot()
{
    double maxX = static_cast<double>(_columnCount);
    double maxY = 0.0;

    std::random_device randomDevice;
    std::mt19937 mTwister(randomDevice());
    std::uniform_int_distribution<> randomColorDist(0, 255);

    // Plot each row individually
    for(auto row : _selectedRows)
    {
        auto* graph = _customPlot.addGraph();
        mTwister.seed(static_cast<unsigned int>(row));
        QColor randomColor = QColor::fromHsl(randomColorDist(mTwister), 210, 130);
        graph->setPen(QPen(randomColor));
        graph->setName(_graphNames[row]);

        QVector<double> yData;
        QVector<double> xData;

        for(size_t col = 0; col < _columnCount; col++)
        {
            auto index = (row * _columnCount) + col;
            xData.append(static_cast<double>(col));
            yData.append(_data[static_cast<int>(index)]);

            maxY = std::max(maxY, _data[static_cast<int>(index)]);
        }
        graph->setData(xData, yData, true);
    }

    _customPlot.xAxis->setRange(0.0, maxX);
    _customPlot.yAxis->setRange(0.0, maxY);
}

void CorrelationPlotItem::setSelectedRows(const QVector<int>& selectedRows)
{
    _selectedRows = selectedRows;
    refresh();
}

void CorrelationPlotItem::setLabelNames(const QStringList& labelNames)
{
    _labelNames = labelNames;
}

void CorrelationPlotItem::setElideLabelWidth(int elideLabelWidth)
{
    bool changed = (_elideLabelWidth != elideLabelWidth);
    _elideLabelWidth = elideLabelWidth;

    if(changed)
        refresh();
}

void CorrelationPlotItem::setColumnCount(size_t columnCount)
{
    _columnCount = columnCount;
    emit minimumWidthChanged();
}

void CorrelationPlotItem::setShowColumnNames(bool showColumnNames)
{
    bool changed = (_showColumnNames != showColumnNames);
    _showColumnNames = showColumnNames;

    emit minimumWidthChanged();

    if(changed)
        refresh();
}

unsigned int CorrelationPlotItem::minimumWidth() const
{
    QFontMetrics metrics(_defaultFont9Pt);
    const auto& margins = _customPlot.axisRect()->margins();
    const unsigned int axisWidth = margins.left() + margins.right();

    if(!_showColumnNames)
        return axisWidth + 50;

    const unsigned int columnPadding = 1;

    return (static_cast<unsigned int>(_columnCount) *
        (metrics.height() + columnPadding)) + axisWidth;
}

void CorrelationPlotItem::routeMouseEvent(QMouseEvent* event)
{
    auto* newEvent = new QMouseEvent(event->type(), event->localPos(),
                                     event->button(), event->buttons(),
                                     event->modifiers());
    QCoreApplication::postEvent(&_customPlot, newEvent);
}

void CorrelationPlotItem::routeWheelEvent(QWheelEvent* event)
{
    auto* newEvent = new QWheelEvent(event->pos(), event->delta(),
                                     event->buttons(), event->modifiers(),
                                     event->orientation());
    QCoreApplication::postEvent(&_customPlot, newEvent);
}

void CorrelationPlotItem::updateCustomPlotSize()
{
    _customPlot.setGeometry(0, 0, static_cast<int>(width()), static_cast<int>(height()));
}

void CorrelationPlotItem::showTooltip()
{
    auto graph = dynamic_cast<QCPGraph*>(_hoverPlottable);
    Q_ASSERT(graph != nullptr);

    _itemTracer->setGraph(graph);
    _itemTracer->setVisible(true);
    _itemTracer->setInterpolating(false);
    _itemTracer->setGraphKey(_customPlot.xAxis->pixelToCoord(_hoverPoint.x()));

    _hoverLabel->setVisible(true);
    _hoverLabel->position->setPixelPosition(QPointF(_itemTracer->anchor("position")->pixelPosition().x() + 10.0f,
                                                    _itemTracer->anchor("position")->pixelPosition().y()));

    _hoverLabel->setText(QString("%1, %2: %3")
                         .arg(_hoverPlottable->name())
                         .arg(_labelNames[static_cast<int>(_itemTracer->position->key())])
                         .arg(_itemTracer->position->value())
                         );

    _hoverColorRect->setVisible(true);
    _hoverColorRect->setBrush(QBrush(_hoverPlottable->pen().color()));
    _hoverColorRect->bottomRight->setPixelPosition(QPointF(_hoverLabel->bottomRight->pixelPosition().x() + 10.0f,
                                                   _hoverLabel->bottomRight->pixelPosition().y()));

    _textLayer->replot();

    update();
}

void CorrelationPlotItem::hideTooltip()
{
    _hoverLabel->setVisible(false);
    _hoverColorRect->setVisible(false);
    _itemTracer->setVisible(false);
    _textLayer->replot();
    update();
}

void CorrelationPlotItem::savePlotImage(const QUrl& url, const QString& format)
{
    if(format.contains("png"))
        _customPlot.savePng(url.toLocalFile());
    else if(format.contains("pdf"))
        _customPlot.savePdf(url.toLocalFile());
    else if(format.contains("jpg"))
        _customPlot.saveJpg(url.toLocalFile());

    QDesktopServices::openUrl(url);
}

void CorrelationPlotItem::onCustomReplot()
{
    update();
}
