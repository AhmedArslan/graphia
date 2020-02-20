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

#ifndef USERELEMENTDATA_H
#define USERELEMENTDATA_H

#include "userdata.h"

#include "shared/graph/grapharray.h"
#include "shared/graph/imutablegraph.h"
#include "shared/graph/igraphmodel.h"
#include "shared/attributes/iattribute.h"
#include "shared/utils/container.h"
#include "shared/utils/progressable.h"

#include <map>
#include <memory>

template<typename E>
class UserElementData : public UserData
{
private:
    struct Index
    {
        bool _set = false;
        size_t _value = 0;
    };

    std::unique_ptr<ElementIdArray<E, Index>> _indexes;
    std::map<size_t, E> _indexToElementIdMap;

    void generateElementIdMapping(E elementId)
    {
        if(_indexes->get(elementId)._set)
        {
            // Already got one
            return;
        }

        _indexes->set(elementId, {true, static_cast<size_t>(numValues())});
        _indexToElementIdMap[numValues()] = elementId;
    }

public:
    void initialise(IMutableGraph& mutableGraph)
    {
        _indexes = std::make_unique<ElementIdArray<E, Index>>(mutableGraph);
    }

    void setElementIdForIndex(E elementId, size_t index)
    {
        _indexes->set(elementId, {true, index});
        _indexToElementIdMap[index] = elementId;
    }

    E elementIdForIndex(size_t index) const
    {
        if(u::contains(_indexToElementIdMap, index))
            return _indexToElementIdMap.at(index);

        // This can happen if the user has deleted some nodes then saved and reloaded
        // In this case the ElementIds may no longer exist for the index in question
        return {};
    }

    size_t indexFor(E elementId) const
    {
        return _indexes->get(elementId)._value;
    }

    void setValueBy(E elementId, const QString& name, const QString& value)
    {
        generateElementIdMapping(elementId);
        setValue(indexFor(elementId), name, value);
    }

    QVariant valueBy(E elementId, const QString& name) const
    {
        return value(indexFor(elementId), name);
    }

    void exposeAsAttributes(IGraphModel& graphModel)
    {
        for(const auto& [name, userDataVector] : *this)
        {
            // https://stackoverflow.com/questions/46114214/lambda-implicit-capture-fails-with-variable-declared-from-structured-binding
            const auto& userDataVectorName = name;

            auto& attribute = graphModel.createAttribute(userDataVectorName)
                    .setFlag(AttributeFlag::Searchable)
                    .setUserDefined(true);

            switch(userDataVector.type())
            {
            case UserDataVector::Type::Float:
                attribute.setFloatValueFn([this, userDataVectorName](E elementId)
                        {
                            return valueBy(elementId, userDataVectorName).toFloat();
                        })
                        .setFlag(AttributeFlag::AutoRange);
                break;

            case UserDataVector::Type::Int:
                attribute.setIntValueFn([this, userDataVectorName](E elementId)
                        {
                            return valueBy(elementId, userDataVectorName).toInt();
                        })
                        .setFlag(AttributeFlag::AutoRange);
                break;

            case UserDataVector::Type::String:
                attribute.setStringValueFn([this, userDataVectorName](E elementId)
                        {
                            return valueBy(elementId, userDataVectorName).toString();
                        })
                        .setFlag(AttributeFlag::FindShared);
                break;

            default: break;
            }

            bool hasMissingValues = std::any_of(userDataVector.begin(), userDataVector.end(),
                                                [](const auto& v) { return v.isEmpty(); });

            if(hasMissingValues)
            {
                attribute.setValueMissingFn([this, userDataVectorName](E elementId)
                {
                   return valueBy(elementId, userDataVectorName).toString().isEmpty();
                });
            }

            attribute.setDescription(QString(QObject::tr("%1 is a user defined attribute.")).arg(userDataVectorName));
        }
    }

    json save(const IMutableGraph&, const std::vector<E>& elementIds, Progressable& progressable) const
    {
        std::vector<size_t> indexes;
        json jsonIds = json::array();

        for(auto elementId : elementIds)
        {
            auto index = _indexes->at(elementId);
            if(index._set)
            {
                jsonIds.push_back(elementId);
                indexes.push_back(index._value);
            }
        }

        json jsonObject = UserData::save(progressable, indexes);
        jsonObject["ids"] = jsonIds;

        return jsonObject;
    }

    bool load(const json& jsonObject, Progressable& progressable)
    {
        if(!UserData::load(jsonObject, progressable))
            return false;

        _indexes->resetElements();
        _indexToElementIdMap.clear();

        const char* idsKey = "ids";
        if(!u::contains(jsonObject, idsKey) || !jsonObject[idsKey].is_array())
        {
            // version <= 3 files call it indexes, try that too
            idsKey = "indexes";
            if(!u::contains(jsonObject, idsKey) || !jsonObject[idsKey].is_array())
                return false;
        }

        const auto& ids = jsonObject[idsKey];

        size_t index = 0;
        for(const auto& id : ids)
        {
            E elementId = id.get<int>();
            setElementIdForIndex(elementId, index++);
        }

        return true;
    }
};

using UserNodeData = UserElementData<NodeId>;
using UserEdgeData = UserElementData<EdgeId>;

#endif // USERELEMENTDATA_H
