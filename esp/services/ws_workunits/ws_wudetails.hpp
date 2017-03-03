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

class CWUDetails : public IWuScopeVisitor
{
public:
    CWUDetails(IConstWorkUnit *_workunit, const char *_wuid, IEspWUDetailsRequest &req);
    virtual void getResponse(IEspWUDetailsResponse &resp);
    virtual void startScope(const char * scope, StatisticScopeType type) override;
    virtual void stopScope() override;
    virtual void noteStatistic(StatisticKind kind, unsigned __int64 value, IConstWUStatistic & extra) override;
    virtual void noteAttribute(WuAttr attr, const char * value) override;
    virtual void noteHint(const char * kind, const char * value) override;


private:
    class CAttributeFilter: public CInterface
    {
        StatisticKind statisticKind;
        unsigned __int64 exactValue;
        unsigned __int64 minValue;
        unsigned __int64 maxValue;
        bool hasExactValue;
        bool hasMinValue;
        bool hasMaxValue;
    public:
        IMPLEMENT_IINTERFACE;

        CAttributeFilter()
                        : statisticKind(StKindNone), exactValue(0), minValue(0), maxValue(0),
                        hasExactValue(false), hasMinValue(false), hasMaxValue(false) {};
        CAttributeFilter(StatisticKind sk, unsigned __int64 exact, unsigned __int64 min,
                        unsigned __int64 max, bool _hasExactValue, bool _hasMinValue, bool _hasMaxValue)
                        : statisticKind(sk), exactValue(exact), minValue(min), maxValue(max),
                        hasExactValue(_hasExactValue), hasMinValue(_hasMinValue), hasMaxValue(_hasMaxValue) {};
        StatisticKind getStatisticKind() { return statisticKind; }
        void setStatisticKind(StatisticKind sk) { statisticKind = sk; }
        bool matchesValue(unsigned __int64 graphRawValue)
        {
            bool matches = true;
            if (hasExactValue)
            {
                if (graphRawValue != exactValue) matches = false;
            }
            else
            {
                if (hasMinValue && graphRawValue < minValue) matches = false;
                if (hasMaxValue && graphRawValue > maxValue) matches = false;
            }
            return matches;
        }
    };
    static int funcCompareAttribFilter(CInterface* const * l, CInterface* const * r)
    {
        CAttributeFilter * ll = static_cast<CAttributeFilter *>(*l);
        CAttributeFilter * rr = static_cast<CAttributeFilter *>(*r);

        return (int) ll->getStatisticKind() - (int) rr->getStatisticKind();
    }

    Owned<IConstWorkUnit> workunit;
    StringBuffer wuid;
    IConstWUScopeOptions & scopeOptions;
    IConstWUAttributeOptions & attribOptions;
    unsigned maxDepth;
    unsigned nestedDepth;
    unsigned __int64 minVersion;

    StringArray ids, scopes;
    CIArrayOf<CAttributeFilter> arrayAttributeFilter;
    Owned<IBitSet> attribFilterMask;
    bool returnAttribListSpecified;
    Owned<IBitSet> returnAttribList;
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
    Owned<IBitSet> attribFilterProcessed;
    IArrayOf<IEspWUResponseScope> respScopes;

    void buildAttribFilter(IArrayOf<IConstWUAttributeFilter> & reqAttribFilter);
    void buildAttribListToReturn(IConstWUAttributeToReturn & attribsToReturn);
    void setNewScope(const char *scope, StatisticScopeType type);
    void inline setSkipScope(bool b) { skipScope = b; }
    void resetScope();
    IEspWUResponseScope * buildRespScope();
};

#endif
