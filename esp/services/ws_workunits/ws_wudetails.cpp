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


void processWUDetails(IConstWorkUnit *workunit, const char *_wuid, IEspWUDetailsRequest &req, IEspWUDetailsResponse &resp)
{
     CWUDetails wuDetails(workunit, _wuid, req);

     Owned<StatisticsFilter> filter(wuDetails.getStatisticsFilter());

     Owned<IConstWUScopeIterator> iter = &workunit->getScopeIterator(filter);
     ForEach(*iter)
     {
         wuDetails.startScope(iter->queryScope(), iter->getScopeType());

         if (!wuDetails.statisticsMatchesFilter(iter) || !wuDetails.attributesMatchesFilter(iter))
            continue;

         iter->playProperties(wuDetails);
         wuDetails.stopScope();
     }
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
    if (mask==0)
      mask=-1; // If no scope type filter, then filtering on scope type disabled

    return mask;
}

static const unsigned StatisticFilterMaskSize = StatisticKind::StMax;
static const unsigned AttributeFilterMaskSize = WuAttr::WAMax-WANone;

CWUDetails::CWUDetails(IConstWorkUnit *_workunit, const char *_wuid, IEspWUDetailsRequest &req)
: workunit(_workunit), wuid(_wuid), scopeOptions(req.getScopeOptions()),
  attribOptions(req.getAttributeOptions()), maxTimestamp(0)
{
    IConstWUScopeFilter & scopeFilter = req.getFilter();
    IConstWUAttributeToReturn & attribsToReturn = req.getAttributeToReturn();
    IConstWUNestedFilter & nfilter = req.getNested();

    ids.appendList(scopeFilter.getIds());
    scopes.appendList(scopeFilter.getScopes());
    maxDepth  = scopeFilter.getMaxDepth();
    nestedDepth = nfilter.getDepth();

    __int64 reqMinVersion = atoi64(attribsToReturn.getMinVersion());
    // minVersion==0 => keep minVersion unchanged so that scopes returned regardless of version number
    // otherise => increment so that the previously return scopes are not returned
    minVersion = (reqMinVersion>0) ? reqMinVersion+1 : 0;

    returnStatisticListSpecified=false;
    returnStatisticList.set(createBitSet(StatisticFilterMaskSize,true));

    returnAttributeSpecified = false;
    returnAttributeList.set(createBitSet(AttributeFilterMaskSize,true));

    buildAttribListToReturn(attribsToReturn);
    buildAttribFilter(scopeFilter.getAttributeFilters());
    nestedScopeTypeFilterMask = buildScopeTypeMaskFromStringArray(nfilter.getScopeTypes());
    scopeTypeFilterMask = buildScopeTypeMaskFromStringArray(scopeFilter.getScopeTypes());

    ids.pruneEmpty();
    ids.sortAscii();      // Will need to lookup up 'id' in ids (for bSearch)
    scopes.pruneEmpty();
    scopes.sortAscii();  // needed when searching nested scope

    // If a compile time error is reported here then the number of enums in StatisticScopeType has grown so that
    // an 'unsigned' is too small to be used as a mask for all the values in StatisticScopeType
    static_assert(StatisticScopeType::SSTmax<sizeof(unsigned)*8, "scopeTypeFilterMask and nestedScopeTypeFilterMask mask too small hold all values");

    filterByScope = scopes.ordinality()>0;
    filterById = ids.ordinality()>0;
    resetScope();
}

StatisticsFilter * CWUDetails::getStatisticsFilter()
{
    return new StatisticsFilter("*", "*", "*", "*", NULL, "*");
}

void CWUDetails::getResponse(IEspWUDetailsResponse &resp)
{
    StringBuffer maxVersion;
    maxVersion.append(maxTimestamp);

    resp.setWUID(wuid.str());
    resp.setMaxVersion(maxVersion.str());
    resp.setScopes(respScopes);
}

void CWUDetails::startScope(const char * scope, StatisticScopeType type)
{
    unsigned scopeDepth = getScopeDepth(scope);
    if (maxDepth && scopeDepth > maxDepth)
    {
        // Scope is deeper than maxDepth, check if scope is valid for nested conditions
        // Note: this code relies on startScope being called in parent->child scope order
        if (!nestedDepth || (scopeDepth > maxDepth + nestedDepth) ||
            !isLeftScopeChildOfRightScope(scope, matchedScope.str()) ||
            (scopeDepth > matchedScopeDepth + nestedDepth) ||
            ((nestedScopeTypeFilterMask & (1<<type))==0))
            return setSkipScope(true);
    }
    else
    {
        if ((scopeTypeFilterMask & (1<<type))==0)
            return setSkipScope(true);

        const char *id = getScopeIdFromScopeName(scope);
        if (filterById && ids.bSearch(id,funcCompareString)==NotFound)
            return setSkipScope(true);

        if (filterByScope && scopes.bSearch(scope, funcCompareString)==NotFound)
            return setSkipScope(true);
        matchedScope.set(scope);
        matchedScopeDepth = scopeDepth;
        if (!scopeOptions.getIncludeMatchedScopesInResults())
            return setSkipScope(true);
    }
    setNewScope(scope, type);
}

void CWUDetails::stopScope()
{
    if (!skipScope && hasScopeStarted)
    {
        Owned<IEspWUResponseScope> respScope = buildRespScope();
        respScopes.append(*respScope.getClear());
    }
    resetScope();
}

void CWUDetails::noteStatistic(StatisticKind kind, unsigned __int64 value, IConstWUStatistic & extra)
{
    if (skipScope) return;

    unsigned __int64 timeStamp = extra.getTimestamp();
    if (timeStamp<minVersion)
        return;
    if (timeStamp>maxTimestamp)
        maxTimestamp = timeStamp;

    // Check attribute is in list of return attribute
    if (returnStatisticListSpecified && !returnStatisticList->test(kind))
        return;

    Owned<IEspWUResponseAttribute> attrib = createWUResponseAttribute("","");
    SCMStringBuffer tmpStr;;

    if (kind != StKindNone)
        attrib->setName(queryStatisticName(kind));
    if (attribOptions.getIncludeRawValue())
    {
        StringBuffer rawValue;
        rawValue.append(value);
        attrib->setRawValue(rawValue);
    }
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

void CWUDetails::noteAttribute(WuAttr attr, const char * value)
{
    if (skipScope) return;


    if (returnAttributeSpecified && !returnAttributeList->test(attr))
        return;

    Owned<IEspWUResponseAttribute> attrib = createWUResponseAttribute("","");

    attrib->setName(queryWuAttributeName(attr));
    if (attribOptions.getIncludeFormatted())
        attrib->setFormatted(value);

    currentAttribs.append(*attrib.getClear());
}

void CWUDetails::noteHint(const char * kind, const char * value)
{
    if (skipScope) return;

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

void CWUDetails::buildAttribFilter(IArrayOf<IConstWUAttributeFilter> & reqAttribFilter)
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

void CWUDetails::buildAttribListToReturn(IConstWUAttributeToReturn & attribsToReturn)
{
    StringArray & attribsToReturnList = attribsToReturn.getAttributes();
    returnStatisticListSpecified=false;
    ForEachItemIn(idx,attribsToReturnList)
    {
        const char * attributeName = attribsToReturnList[idx];
        if (*attributeName==0) continue;

        StatisticKind sk = queryStatisticKind(attributeName);
        if (sk==StKindAll)
            throw MakeStringException(ECLWATCH_INVALID_INPUT, "Invalid Attribute name in AttributeToReturn(%s)",attributeName);
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
                throw MakeStringException(ECLWATCH_INVALID_INPUT, "Invalid Attribute name in AttributeToReturn(%s)",attributeName);
        }
    }
}

void CWUDetails::setNewScope(const char *scope, StatisticScopeType type)
{
    hasScopeStarted = true;
    setSkipScope(false);
    currentScope.set(scope);
    currentScopeType = type;
    currentAttribs.clear();
}

void CWUDetails::resetScope()
{
    hasScopeStarted = false;
    setSkipScope(false);
    currentScope.clear();
    currentAttribs.clear();
}

IEspWUResponseScope * CWUDetails::buildRespScope()
{
    const char * scopeType = queryScopeTypeName(currentScopeType);
    const char * id =  getScopeIdFromScopeName(currentScope);

    Owned<IEspWUResponseScope> wuscope = createWUResponseScope("","");

    if (scopeOptions.getIncludeScope())     wuscope->setScope(currentScope);
    if (scopeOptions.getIncludeScopeType()) wuscope->setScopeType(scopeType);
    if (scopeOptions.getIncludeId())        wuscope->setId(id);

    wuscope->setAttributes(currentAttribs);

    return wuscope.getClear();
}

bool CWUDetails::statisticsMatchesFilter(IConstWUScopeIterator *iter)
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

bool CWUDetails::attributesMatchesFilter(IConstWUScopeIterator *iter)
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
