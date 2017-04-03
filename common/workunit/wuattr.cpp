/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2012 HPCC Systems®.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
############################################################################## */

#include "wuattr.hpp"
#include "jptree.hpp"

struct WuAttrInfo
{
public:
    WuAttr kind;
    StatisticMeasure measure;
    const char * name;
    const char * graphId;
    const char * dft;
};

#define CHILDPATH(x) "att[@name='" x "']/@value"
#define ATTR(kind, measure, path)           { WA ## kind, measure, #kind, path, nullptr }
#define CHILD(kind, measure, path)    { WA ## kind, measure, #kind, CHILDPATH(path), nullptr }
#define CHILD_D(kind, measure, path, dft)    { WA ## kind, measure, #kind, CHILDPATH(path), dft }


const static WuAttrInfo attrInfo[] = {
    { WANone, SMeasureNone, "none", nullptr, nullptr },
    CHILD(Kind, SMeasureEnum, "_kind"),
    ATTR(Source, SMeasureText, "@source"),
    ATTR(Target, SMeasureText, "@target"),
    CHILD_D(SourceIndex, SMeasureText, "_sourceIndex", "0"),
    CHILD_D(TargetIndex, SMeasureText, "_targetIndex", "0"),
    { WAMax, SMeasureNone, nullptr, nullptr, nullptr }
};


MODULE_INIT(INIT_PRIORITY_STANDARD)
{
    static_assert(_elements_in(attrInfo) == (WAMax-WANone)+1, "Elements missing from attrInfo[]");
    for (unsigned i=0; i < _elements_in(attrInfo); i++)
    {
        assertex(attrInfo[i].kind == WANone + i);
    }
    return true;
}


const char * queryWuAttributeName(WuAttr kind)
{
    if ((kind >= WANone) && (kind < WAMax))
        return attrInfo[kind-WANone].name;
    return nullptr;
}

WuAttr queryWuAttribute(const char * kind)
{
    //MORE: This needs to use a hash table
    for (unsigned i=WANone; i < WAMax; i++)
    {
        if (strieq(kind, attrInfo[i-WANone].name))
            return (WuAttr)i;
    }
    return WANone;
}

extern WORKUNIT_API const char * queryAttributeValue(IPropertyTree & src, WuAttr kind)
{
    if ((kind <= WANone) || (kind >= WAMax))
        return nullptr;


    const WuAttrInfo & info = attrInfo[kind-WANone];
    const char * path = info.graphId;
    const char * value = src.queryProp(path);
    if (!value && info.dft)
        value = info.dft;
    return value;
}
