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

extern void processWUDetails(IConstWorkUnit *workunit, const char *_wuid, IEspWUDetailsRequest &req, IEspWUDetailsResponse &resp);

class CWUDetails : public IWuScopeVisitor
{
public:
    CWUDetails(IConstWorkUnit *_workunit, const char *_wuid, IEspWUDetailsRequest &req);
    virtual StatisticsFilter *getStatisticsFilter();
    virtual void getResponse(IEspWUDetailsResponse &resp);
    virtual void startScope(const char * scope, StatisticScopeType type) override;
    virtual void stopScope() override;
    virtual void noteStatistic(StatisticKind kind, unsigned __int64 value, IConstWUStatistic & extra) override;
    virtual void noteAttribute(WuAttr attr, const char * value) override;
    virtual void noteHint(const char * kind, const char * value) override;

    bool statisticsMatchesFilter(IConstWUScopeIterator *iter);
    bool attributesMatchesFilter(IConstWUScopeIterator *iter);

private:
    class StatisticFilter : public CInterface
    {
        StatisticKind attribId;
        unsigned __int64 exactValue, minValue, maxValue;
        bool hasExactValue, hasMinValue,hasMaxValue;
    public:
        IMPLEMENT_IINTERFACE;

        StatisticFilter(StatisticKind id, const unsigned __int64 exact, const unsigned __int64 min, const unsigned __int64 max,
                        bool _hasExactValue, bool _hasMinValue, bool _hasMaxValue)
                        : attribId(id), exactValue(exact), minValue(min), maxValue(max),
                        hasExactValue(_hasExactValue), hasMinValue(_hasMinValue), hasMaxValue(_hasMaxValue) {};
        unsigned getAttributeId() const { return attribId; }
        bool matchesValue(const unsigned __int64 value) const
        {
            bool matches = true;
            if (hasExactValue)
                matches = (exactValue==value);
            else
            {
                if (hasMinValue && value < minValue) matches = false;
                if (hasMaxValue && value > maxValue) matches = false;
            }
            return matches;
        };
    };
    class AttributeFilter : public CInterface
    {
        WuAttr attribId;
        String exactValue, minValue, maxValue;
        bool hasExactValue, hasMinValue, hasMaxValue;
    public:
        IMPLEMENT_IINTERFACE;

        AttributeFilter(WuAttr id, const char * exact, const char * min, const  char * max,
                        bool _hasExactValue, bool _hasMinValue, bool _hasMaxValue)
                        : attribId(id), exactValue(exact), minValue(min), maxValue(max),
                        hasExactValue(_hasExactValue), hasMinValue(_hasMinValue), hasMaxValue(_hasMaxValue) {};
        unsigned getAttributeId() const { return attribId; }
        bool matchesValue(const char * value)
        {
            bool matches = true;
            if (hasExactValue)
                matches = exactValue.compareTo(value)==0;
            else
            {
                if (hasMinValue && minValue.compareTo(value)>0) matches = false;
                if (hasMaxValue && maxValue.compareTo(value)<0) matches = false;
            }
            return matches;
        };
    };

    Owned<IConstWorkUnit> workunit;
    StringBuffer wuid;
    IConstWUScopeOptions & scopeOptions;
    IConstWUAttributeOptions & attribOptions;
    unsigned maxDepth;
    unsigned nestedDepth;
    unsigned __int64 minVersion;

    StringArray ids, scopes;
    CIArrayOf<StatisticFilter> arrayStatisticFilter;
    CIArrayOf<AttributeFilter> arrayAttribFilter;

    bool returnStatisticListSpecified;
    Owned<IBitSet> returnStatisticList;
    bool returnAttributeSpecified;
    Owned<IBitSet> returnAttributeList;
    unsigned scopeTypeFilterMask;
    unsigned nestedScopeTypeFilterMask;
    bool filterByScope;
    bool filterById;

    bool hasScopeStarted;
    bool skipScope;
    unsigned __int64 maxTimestamp;
    StringBuffer matchedScope;
    unsigned matchedScopeDepth;
    StringBuffer currentScope;
    StatisticScopeType currentScopeType;
    IArrayOf<IEspWUResponseAttribute> currentAttribs;
    IArrayOf<IEspWUResponseScope> respScopes;

    void buildAttribFilter(IArrayOf<IConstWUAttributeFilter> & reqAttribFilter);
    void buildAttribListToReturn(IConstWUAttributeToReturn & attribsToReturn);
    void setNewScope(const char *scope, StatisticScopeType type);
    void inline setSkipScope(bool b) { skipScope = b; }
    void resetScope();
    IEspWUResponseScope * buildRespScope();

};

#endif
