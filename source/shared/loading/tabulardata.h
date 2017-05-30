#ifndef TABULARDATA_H
#define TABULARDATA_H

#include "shared/graph/imutablegraph.h"
#include "shared/loading/baseparser.h"

#include "thirdparty/utfcpp/utf8.h"

#include <QUrl>

#include <string>
#include <vector>
#include <iostream>
#include <fstream>

class TabularData
{
private:
    std::vector<std::string> _data;
    size_t _columns = 0;
    size_t _rows = 0;
    bool _transposed = false;

    size_t index(size_t column, size_t row) const;

public:
    void initialise(size_t numColumns, size_t numRows);

    size_t numColumns() const;
    size_t numRows() const;
    bool transposed() const { return _transposed; }
    const std::string& valueAt(size_t column, size_t row) const;
    QString valueAsQString(size_t column, size_t row) const;

    void setTransposed(bool transposed) { _transposed = transposed; }
    void setValueAt(size_t column, size_t row, std::string&& value);
};

template<const char Delimiter> class TextDelimitedTabularDataParser : public BaseParser
{
    static_assert(Delimiter != '\"', "Delimiter cannot be a quotemark");

private:
    TabularData _tabularData;
    const BaseParser* _parentParser = nullptr;

    template<typename TokenFn>
    bool parse(const QUrl& url, const ProgressFn& progress, TokenFn tokenFn)
    {
        std::ifstream file(url.toLocalFile().toStdString());
        if(!file)
            return false;

        auto fileSize = file.tellg();
        file.seekg(0, std::ios::end);
        fileSize = file.tellg() - fileSize;

        std::string line;
        std::string currentToken;
        size_t currentColumn = 0;
        size_t currentRow = 0;

        progress(-1);

        file.seekg(0, std::ios::beg);
        while(std::getline(file, line))
        {
            if(_parentParser != nullptr && _parentParser->cancelled())
                return false;

            bool inQuotes = false;

            std::string validatedLine;
            utf8::replace_invalid(line.begin(), line.end(), std::back_inserter(validatedLine));
            auto it = validatedLine.begin();
            auto end = validatedLine.end();
            while(it < end)
            {
                uint32_t codePoint = utf8::next(it, end);

                if(codePoint == '\"')
                {
                    if(inQuotes)
                    {
                        tokenFn(currentColumn++, currentRow, std::move(currentToken));
                        currentToken.clear();

                        // Quote closed, but there is text before the delimiter
                        while(it < end && codePoint != Delimiter)
                            codePoint = utf8::next(it, end);
                    }

                    inQuotes = !inQuotes;
                }
                else
                {
                    bool delimiter = (codePoint == Delimiter);

                    if(delimiter && !inQuotes)
                    {
                        tokenFn(currentColumn++, currentRow, std::move(currentToken));
                        currentToken.clear();
                    }
                    else
                        utf8::unchecked::append(codePoint, std::back_inserter(currentToken));
                }
            }

            if(!currentToken.empty())
            {
                tokenFn(currentColumn++, currentRow, std::move(currentToken));
                currentToken.clear();
            }

            currentRow++;
            currentColumn = 0;

            progress(static_cast<int>(file.tellg() * 100 / fileSize));
        }

        return true;
    }

public:
    bool parse(const QUrl& url, IMutableGraph& graph, const ProgressFn& progress)
    {
        size_t columns = 0;
        size_t rows = 0;

        // First pass to determine the size of the table
        graph.setPhase(QObject::tr("Finding size"));
        bool success = parse(url, progress,
        [&columns, &rows](size_t column, size_t row, auto)
        {
            columns = std::max(columns, column + 1);
            rows = std::max(rows, row + 1);
        });

        if(!success)
            return false;

        _tabularData.initialise(columns, rows);

        graph.setPhase(QObject::tr("Parsing"));
        return parse(url, progress,
        [this](size_t column, size_t row, auto&& token)
        {
            _tabularData.setValueAt(column, row, std::move(token));
        });
    }

    void setParentParser(const BaseParser* parentParser) { _parentParser = parentParser; }

    TabularData& tabularData() { return _tabularData; }
};

using CsvFileParser = TextDelimitedTabularDataParser<','>;
using TsvFileParser = TextDelimitedTabularDataParser<'\t'>;

#endif // TABULARDATA_H
