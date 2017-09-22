/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2017 HPCC Systems®.

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

#ifndef _ESPWIZ_ws_wudetails_HPP__
#define _ESPWIZ_ws_wudetails_HPP__

#include "workunit.hpp"
#include "jstatcodes.h"


class WUDetails
{
public:
    WUDetails(IConstWorkUnit *_workunit, const char *_wuid, IEspWUDetailsRequest &req);
    void processRequest(IEspWUDetailsResponse &resp);

private:
    Owned<IConstWorkUnit> workunit;
    WuScopeFilter wuScopeFilter;
    StringBuffer wuid;
    IConstWUScopeFilter & requestScopeFilter;
    IConstWUNestedFilter & nestedfilter;
    IConstWUAttributeToReturn & attribsToReturn;
    IConstWUScopeOptions & scopeOptions;
    IConstWUAttributeOptions & attribOptions;

    void buildWuScopeFilter();
    void buildAttribFilter(IArrayOf<IConstWUAttributeFilter> & reqAttribFilter);
    bool statisticsMatchesFilter(IConstWUScopeIterator *iter);
    bool attributesMatchesFilter(IConstWUScopeIterator *iter);
};


#endif
