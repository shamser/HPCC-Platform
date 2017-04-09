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

#include "ws_workunitsService.hpp"
#include "ws_wudetails.hpp"
#include "jlib.hpp"
#include "workunit.hpp"
#include "jset.hpp"
#include "jstatcodes.h"

static const unsigned StatisticFilterMaskSize = StatisticKind::StMax;
static const unsigned AttributeFilterMaskSize = WuAttr::WAMax-WANone;
class WUDetailsVisitor : public IWuScopeVisitor
{
public:
    WUDetailsVisitor(IConstWUAttributeOptions & _attribOptions, IConstWUAttributeToReturn & _attribsToReturn);
    virtual void startScope(const char * scope, StatisticScopeType type) override {};
    virtual void stopScope() override {};
    virtual void noteStatistic(StatisticKind kind, unsigned __int64 value, IConstWUStatistic & extra) override;
    virtual void noteAttribute(WuAttr attr, const char * value) override;
    virtual void noteHint(const char * kind, const char * value) override;

    void resetScope();
    IArrayOf<IEspWUResponseAttribute> & getCurrentAttribs() { return currentAttribs;}
    unsigned __int64 getMaxTimestamp() const { return maxTimestamp;}

private:
    IConstWUAttributeOptions & attribOptions;
    IConstWUAttributeToReturn & attribsToReturn;
    unsigned __int64 minVersion;

    bool returnStatisticListSpecified;
    Owned<IBitSet> returnStatisticList;
    bool returnAttributeSpecified;
    Owned<IBitSet> returnAttributeList;
    bool onlyDynamic;
    unsigned __int64 maxTimestamp;
    IArrayOf<IEspWUResponseAttribute> currentAttribs;

    void buildAttribListToReturn(IConstWUAttributeToReturn & attribsToReturn);
};

WUDetailsVisitor::WUDetailsVisitor(IConstWUAttributeOptions & _attribOptions, IConstWUAttributeToReturn & _attribsToReturn)
:  attribOptions(_attribOptions), attribsToReturn(_attribsToReturn), onlyDynamic(_attribsToReturn.getOnlyDynamic()), maxTimestamp(0)
{
    __int64 reqMinVersion = atoi64(attribsToReturn.getMinVersion());
    // minVersion==0 => keep minVersion unchanged so that scopes returned regardless of version number
    // otherise => increment so that the previously return scopes are not returned
    minVersion = (reqMinVersion>0) ? reqMinVersion+1 : 0;

    if (*attribsToReturn.getMeasure())
        onlyDynamic=true;

    buildAttribListToReturn(attribsToReturn);
}

void WUDetailsVisitor::noteStatistic(StatisticKind kind, unsigned __int64 value, IConstWUStatistic & extra)
{
    unsigned __int64 timeStamp = extra.getTimestamp();
    if (timeStamp<minVersion)
        return;
    if (timeStamp>maxTimestamp)
        maxTimestamp = timeStamp;

    // Check attribute is in list of return attribute
    if (returnStatisticListSpecified && !returnStatisticList->test(kind))
        return;

    Owned<IEspWUResponseAttribute> attrib = createWUResponseAttribute("","");
 
    if (kind != StKindNone)
        attrib->setName(queryStatisticName(kind));
    if (attribOptions.getIncludeRawValue())
    {
        StringBuffer rawValue;
        rawValue.append(value);
        attrib->setRawValue(rawValue);
    }
    SCMStringBuffer tmpStr;
    if (attribOptions.getIncludeFormatted())
        attrib->setFormatted(extra.getFormattedValue(tmpStr).str());
    if (attribOptions.getIncludeMeasure() && extra.getMeasure()!= SMeasureNone)
        attrib->setMeasure(queryMeasureName(extra.getMeasure()));
    if (attribOptions.getIncludeCreator())
        attrib->setCreator(extra.getCreator(tmpStr).str());
    if (attribOptions.getIncludeCreatorType() && extra.getCreatorType() != SCTnone)
        attrib->setCreatorType(queryCreatorTypeName(extra.getCreatorType()));

    currentAttribs.append(*attrib.getClear());
}

void WUDetailsVisitor::noteAttribute(WuAttr attr, const char * value)
{
    if (onlyDynamic) return;

    if (returnAttributeSpecified && !returnAttributeList->test(attr-WANone))
        return;

    Owned<IEspWUResponseAttribute> attrib = createWUResponseAttribute("","");

    attrib->setName(queryWuAttributeName(attr));
    if (attribOptions.getIncludeFormatted())
        attrib->setFormatted(value);

    currentAttribs.append(*attrib.getClear());
}

void WUDetailsVisitor::noteHint(const char * kind, const char * value)
{
    if (onlyDynamic) return;

    if (returnAttributeSpecified || returnStatisticListSpecified)
        return;

    StringBuffer hint("hint:");
    hint.append(kind);

    Owned<IEspWUResponseAttribute> attrib = createWUResponseAttribute("","");

    attrib->setName(hint);
    if (attribOptions.getIncludeFormatted())
        attrib->setFormatted(value);

    currentAttribs.append(*attrib.getClear());
}

void WUDetailsVisitor::buildAttribListToReturn(IConstWUAttributeToReturn & attribsToReturn)
{
    returnStatisticListSpecified=false;
    returnStatisticList.set(createBitSet(StatisticFilterMaskSize,true));
    returnAttributeSpecified = false;
    returnAttributeList.set(createBitSet(AttributeFilterMaskSize,true));
    StringArray & attribsToReturnList = attribsToReturn.getAttributes();
    ForEachItemIn(idx,attribsToReturnList)
    {
        const char * attributeName = attribsToReturnList[idx];
        if (*attributeName==0) continue;

        StatisticKind sk = queryStatisticKind(attributeName);
        if (sk==StKindAll)
            throw MakeStringException(ECLWATCH_INVALID_INPUT, "Invalid Attribute name in AttributeToReturn(%s)", attributeName);
        if (sk!=StKindNone)
        {
            returnStatisticList->set(sk, true);
            returnStatisticListSpecified=true;
        }
        else
        {
          WuAttr wa = queryWuAttribute(attributeName);
          if (wa!=WANone)
          {
              returnAttributeList->set(wa-WANone,true);
              returnAttributeSpecified=true;
          }
          else
              throw MakeStringException(ECLWATCH_INVALID_INPUT, "Invalid Attribute name in AttributeToReturn(%s)", attributeName);
        }
    }
}

void WUDetailsVisitor::resetScope()
{
    currentAttribs.clear();
}

static int funcCompareString(char const * const *l, char const * const *r)
{
    return strcmp(*l, *r);
}

static int isLeftScopeChildOfRightScope(const char * leftScope, const char * rightScope)
{
    const char * l = leftScope;
    const char * r = rightScope;

    while (*l && *l==*r)
    {
      ++l;
      ++r;
    }
    if ( *l==':' && *r==0)
      return true;
    return false;
}

inline unsigned getScopeDepth(const char * scope)
{
    unsigned depth = 1;

    for (const char * p = scope; *p; ++p)
        if (*p==':') ++depth;
    return depth;
}

static const char * getScopeIdFromScopeName(const char * scopeName)
{
    const char * p = strrchr(scopeName, ':');
    return p?p+1:scopeName;
}

static unsigned buildScopeTypeMaskFromStringArray(const StringArray & arrayScopeTypeList)
{
    unsigned mask = 0;
    ForEachItemIn(idx, arrayScopeTypeList)
    {
        const char * scopeType = arrayScopeTypeList[idx];
        if (*scopeType==0) continue;

        StatisticScopeType sst = queryScopeType(scopeType);
        if (sst==SSTnone ||sst==SSTall)
          throw MakeStringException(ECLWATCH_INVALID_INPUT, "Invalid ScopeType(%s) in filterNestedScopeTypes",scopeType);
        mask |= (1<<sst);
    }
    if (mask==0) mask=-1; // If no scope type filter, then filtering on scope type disabled

    return mask;
}

WUDetails::WUDetails(IConstWorkUnit *_workunit, const char *_wuid, IEspWUDetailsRequest &req)
: workunit(_workunit), wuid(_wuid), scopeOptions(req.getScopeOptions()), 
attribOptions(req.getAttributeOptions()), attribsToReturn(req.getAttributeToReturn())
{
    IConstWUScopeFilter & scopeFilter = req.getFilter();
    ids.appendList(scopeFilter.getIds());
    scopes.appendList(scopeFilter.getScopes());
    maxDepth  = scopeFilter.getMaxDepth();    

    IConstWUNestedFilter & nfilter = req.getNested();
    nestedDepth = nfilter.getDepth();

    nestedScopeTypeFilterMask = buildScopeTypeMaskFromStringArray(nfilter.getScopeTypes());
    scopeTypeFilterMask = buildScopeTypeMaskFromStringArray(scopeFilter.getScopeTypes());

    ids.pruneEmpty();
    ids.sortAscii();
    scopes.pruneEmpty();
    scopes.sortAscii();

    // If a compile time error is reported here then the number of enums in StatisticScopeType has grown so that
    // an 'unsigned' is too small to be used as a mask for all the values in StatisticScopeType
    static_assert(StatisticScopeType::SSTmax<sizeof(unsigned)*8, "scopeTypeFilterMask and nestedScopeTypeFilterMask mask too small hold all values");

    filterByScope = scopes.ordinality()>0;
    filterById = ids.ordinality()>0;
    buildAttribFilter(scopeFilter.getAttributeFilters());
}

void WUDetails::processRequest(IEspWUDetailsResponse &resp)
{
    IArrayOf<IEspWUResponseScope> respScopes;
    WUDetailsVisitor wuDetailsVisitor(attribOptions, attribsToReturn);

    Owned<StatisticsFilter> filter(getStatisticsFilter());
    StringBuffer matchedScope;
    unsigned matchedScopeDepth;

    Owned<IConstWUScopeIterator> iter = &workunit->getScopeIterator(filter);
    ForEach(*iter)
    {
        const char * scope = iter->queryScope();
        StatisticScopeType scopeType = iter->getScopeType();

        unsigned scopeDepth = getScopeDepth(scope);
        if (maxDepth && scopeDepth > maxDepth)
        {
          // Scope is deeper than maxDepth, check if scope is valid for nested conditions
          // Note: this code relies on startScope being called in parent->child scope order
          if (!nestedDepth || (scopeDepth > maxDepth + nestedDepth) ||
            !isLeftScopeChildOfRightScope(scope, matchedScope.str()) ||
            (scopeDepth > matchedScopeDepth + nestedDepth) ||
            ((nestedScopeTypeFilterMask & (1<<scopeType))==0))
              continue;
        }
        else
        {
          if ((scopeTypeFilterMask & (1<<scopeType))==0)
              continue;

          const char *id = getScopeIdFromScopeName(scope);
          if (filterById && ids.bSearch(id,funcCompareString)==NotFound)
              continue;

          if (filterByScope && scopes.bSearch(scope, funcCompareString)==NotFound)
              continue;

          matchedScope.set(scope);
          matchedScopeDepth = scopeDepth;
          if (!scopeOptions.getIncludeMatchedScopesInResults())
              continue;
        }

        if (!statisticsMatchesFilter(iter) || !attributesMatchesFilter(iter))
          continue;

        iter->playProperties(wuDetailsVisitor);

        Owned<IEspWUResponseScope> respScope = createWUResponseScope("","");
        if (scopeOptions.getIncludeScope())     respScope->setScope(scope);
        if (scopeOptions.getIncludeScopeType()) respScope->setScopeType(queryScopeTypeName(scopeType));
        if (scopeOptions.getIncludeId())        respScope->setId(getScopeIdFromScopeName(scope));

        IArrayOf<IEspWUResponseAttribute> & attribs = wuDetailsVisitor.getCurrentAttribs();
        respScope->setAttributes(attribs);
        respScopes.append(*respScope.getClear());

        wuDetailsVisitor.resetScope();
    }
    StringBuffer maxVersion;
    maxVersion.append(wuDetailsVisitor.getMaxTimestamp());

    resp.setWUID(wuid.str());
    resp.setMaxVersion(maxVersion.str());
    resp.setScopes(respScopes);
}

void WUDetails::buildAttribFilter(IArrayOf<IConstWUAttributeFilter> & reqAttribFilter)
{
    ForEachItemIn(idx,reqAttribFilter)
    {
        IConstWUAttributeFilter & attr = reqAttribFilter.item(idx);
        const char * attributeName = attr.getName();
        if ( *attributeName==0 ) continue; // skip nulls

        const char * exactValue = attr.getExactValue();
        const char * minValue = attr.getMinValue();
        const char * maxValue = attr.getMaxValue();
        bool hasExactValue = *exactValue!=0;
        bool hasMinValue = *minValue!=0;
        bool hasMaxValue = *maxValue!=0;

        if (hasExactValue && (hasMinValue||hasMaxValue))
            throw MakeStringException(ECLWATCH_INVALID_INPUT,
                                      "Invalid Attribute Filter ('%s') - ExactValue may not be used with MinValue or MaxValue",
                                      attributeName);

        StatisticKind sk = queryStatisticKind(attributeName);
        if (sk==StKindAll)
            throw MakeStringException(ECLWATCH_INVALID_INPUT, "Invalid Attribute Name ('%s') in Attribute Filter", attributeName);
        if (sk!=StKindNone)
        {
            Owned<StatisticFilter> caf = new StatisticFilter(sk, atoi64(exactValue), atoi64(minValue), atoi64(maxValue),
                                                            hasExactValue, hasMinValue, hasMaxValue);
            arrayStatisticFilter.append(*caf.getClear());
        }
        else
        {
            WuAttr wa = queryWuAttribute(attributeName);
            if (wa!=WANone)
            {
                Owned<AttributeFilter> caf = new AttributeFilter(wa, exactValue, minValue, maxValue,
                                                              hasExactValue, hasMinValue, hasMaxValue);
                arrayAttribFilter.append(*caf.getClear());
            }
            else
                throw MakeStringException(ECLWATCH_INVALID_INPUT, "Invalid Attribute Name ('%s') in Attribute Filter",attributeName);
        }
    }
}

StatisticsFilter * WUDetails::getStatisticsFilter()
{
    Owned<StatisticsFilter> sf = new StatisticsFilter("*", "*", "*", "*", NULL, "*");

    const char * measure = attribsToReturn.getMeasure();
    if (*measure!=0)
    {
        StatisticMeasure sm = queryMeasure(measure);
        if (sm==SMeasureNone)
            throw MakeStringException(ECLWATCH_INVALID_INPUT, "Invalid measure(%s)", measure);
        if (sm!=SMeasureAll)
            sf->setMeasure(sm);
    }

    sf->setScopeDepth(0,maxDepth+nestedDepth);

    return sf.getClear();
}


bool WUDetails::statisticsMatchesFilter(IConstWUScopeIterator *iter)
{
    ForEachItemIn(idx, arrayStatisticFilter)
    {
        StatisticFilter & statisticFilter  = arrayStatisticFilter.item(idx);

        StatisticKind sk = (StatisticKind)statisticFilter.getAttributeId();
        unsigned __int64 value;
        if (!iter->getStat(sk,value))
            return false;
        if (!statisticFilter.matchesValue(value))
            return false;
    }
    return true;
}

bool WUDetails::attributesMatchesFilter(IConstWUScopeIterator *iter)
{
    ForEachItemIn(idx, arrayAttribFilter)
    {
        AttributeFilter & attribFilter  = arrayAttribFilter.item(idx);

        WuAttr wuAttr = (WuAttr)attribFilter.getAttributeId();
        const char * value = iter->queryAttribute(wuAttr);

        if (!value || !attribFilter.matchesValue(value))
            return false;
    }
    return true;
}


