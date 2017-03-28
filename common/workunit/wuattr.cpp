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

struct WuAttrInfo
{
public:
    WuAttr kind;
    StatisticMeasure measure;
    const char * name;
    const char * graphId;
};

const static WuAttrInfo attrInfo[] = {
    { WAnone, SMeasureNone, "none", nullptr },
    { WAkind, SMeasureEnum, "kind", "_kind" },
    { WAmax, SMeasureNone, nullptr, nullptr }
};


MODULE_INIT(INIT_PRIORITY_STANDARD)
{
    static_assert(_elements_in(attrInfo) == (WAmax-WAnone)+1, "Elements missing from attrInfo[]");
    for (unsigned i=0; i < _elements_in(attrInfo); i++)
    {
        assertex(attrInfo[i].kind == WAnone + i);
    }
    return true;
}


const char * queryWuAttributeName(WuAttr kind)
{
    if ((kind >= WAnone) && (kind < WAmax))
        return attrInfo[kind].name;
    return nullptr;
}

WuAttr queryWuAttribute(const char * kind)
{
    //MORE: This needs to use a hash table
    for (unsigned i=WAnone; i < WAmax; i++)
    {
        if (strieq(kind, attrInfo[i-WAnone].name))
            return (WuAttr)i;
    }
    return WAnone;
}
