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
    virtual void noteStatistic(StatisticKind kind, unsigned __int64 value, IConstWUStatistic & extra) override;
    virtual void noteAttribute(WuAttr attr, const char * value) override;
    virtual void noteHint(const char * kind, const char * value) override;

    void resetScope();
    IArrayOf<IEspWUResponseAttribute> & getCurrentAttribs() { return currentAttribs;}
    unsigned __int64 getMaxTimestamp() const { return maxTimestamp;}

private:
    IConstWUAttributeOptions & attribOptions;
    IConstWUAttributeToReturn & attribsToReturn;

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
    if (*attribsToReturn.getMeasure())
        onlyDynamic=true;

    buildAttribListToReturn(attribsToReturn);
}

void WUDetailsVisitor::noteStatistic(StatisticKind kind, unsigned __int64 value, IConstWUStatistic & extra)
{
    // Check attribute is in list of return attribute
    if (returnStatisticListSpecified && !returnStatisticList->test(kind))
        return;

    Owned<IEspWUResponseAttribute> attrib = createWUResponseAttribute("","");

    if (kind != StKindNone && attribOptions.getIncludeName())
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
    if (extra.getTimestamp()>maxTimestamp)
        maxTimestamp = extra.getTimestamp();
}

void WUDetailsVisitor::noteAttribute(WuAttr attr, const char * value)
{
    if (onlyDynamic) return;

    if (returnAttributeSpecified)
    {
        if (!returnAttributeList->test(attr-WANone))
            return;
    }
    else if (returnStatisticList)
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

static const char * getScopeIdFromScopeName(const char * scopeName)
{
    const char * p = strrchr(scopeName, ':');
    return p?p+1:scopeName;
}

WUDetails::WUDetails(IConstWorkUnit *_workunit, const char *_wuid, IEspWUDetailsRequest &req)
: workunit(_workunit), wuid(_wuid), requestScopeFilter(req.getFilter()),
nestedfilter(req.getNested()), attribsToReturn(req.getAttributeToReturn()),
scopeOptions(req.getScopeOptions()), attribOptions(req.getAttributeOptions()) { }

void WUDetails::buildAttribFilter(IArrayOf<IConstWUAttributeFilter> & reqAttribFilter)
{
    ForEachItemIn(idx4,reqAttribFilter)
    {
        IConstWUAttributeFilter & attribFilterItem = reqAttribFilter.item(idx4);
        const char * attributeName = attribFilterItem.getName();
        if ( *attributeName==0 ) continue; // skip nulls

        bool hasExactValue = *attribFilterItem.getExactValue()!=0;
        bool hasMinValue = *attribFilterItem.getMinValue()!=0;
        bool hasMaxValue = *attribFilterItem.getExactValue()!=0;

        if (hasExactValue && (hasMinValue||hasMaxValue))
            throw MakeStringException(ECLWATCH_INVALID_INPUT,
                                      "Invalid Attribute Filter ('%s') - ExactValue may not be used with MinValue or MaxValue",
                                      attributeName);
        StatisticKind sk = queryStatisticKind(attributeName);
        if (sk==StKindAll || sk==StKindNone)
            throw MakeStringException(ECLWATCH_INVALID_INPUT, "Invalid Attribute Name ('%s') in Attribute Filter", attributeName);
        if (hasExactValue)
        {
            stat_type exactValue = atoi64(attribFilterItem.getExactValue());
            wuScopeFilter.addRequiredStat(sk,exactValue,exactValue);
        }
        else if (hasMinValue||hasMaxValue)
        {
            stat_type minValue = atoi64(attribFilterItem.getMinValue());
            stat_type maxValue = atoi64(attribFilterItem.getExactValue());
            wuScopeFilter.addRequiredStat(sk,minValue,maxValue);
        }
        else
            wuScopeFilter.addRequiredStat(sk);
    }
}


void WUDetails::processRequest(IEspWUDetailsResponse &resp)
{
    IArrayOf<IEspWUResponseScope> respScopes;
    WUDetailsVisitor wuDetailsVisitor(attribOptions, attribsToReturn);

    buildWuScopeFilter();
    WuPropertyTypes pt;
    if (attribsToReturn.getOnlyDynamic() || *attribsToReturn.getMeasure())
        pt = PTstatistics;
    else
        pt = PTall;

    wuScopeFilter.finishedFilter();
    Owned<IConstWUScopeIterator> iter = &workunit->getScopeIterator(wuScopeFilter);
    ForEach(*iter)
    {
        iter->playProperties(pt, wuDetailsVisitor);

        const char * scope = iter->queryScope();
        StatisticScopeType scopeType = iter->getScopeType();
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

void WUDetails::buildWuScopeFilter()
{
    // WuScopeFilter.MaxDepth
    wuScopeFilter.setDepth(0, requestScopeFilter.getMaxDepth());

    // WuScopeFilter.Scopes
    StringArray & scopes = requestScopeFilter.getScopes();
    ForEachItemIn(idx1,scopes)
        if (*scopes.item(idx1))
            wuScopeFilter.addScope(scopes.item(idx1));

    // WuScopeFilter.Ids
    StringArray & ids = requestScopeFilter.getIds();
    ForEachItemIn(idx2,ids)
        if (*ids.item(idx2))
            wuScopeFilter.addId(ids.item(idx2));

    // WuScopeFilter.ScopeTypes
    StringArray & scopeTypes = requestScopeFilter.getScopeTypes();
    ForEachItemIn(idx3,scopeTypes)
        if (*scopeTypes.item(idx3))
            wuScopeFilter.addScopeType(scopeTypes.item(idx3));

    // WuScopeFilter.AttributeFilter
    buildAttribFilter(requestScopeFilter.getAttributeFilters());

    // WuNestedFilter.Depth
    wuScopeFilter.setIncludeNesting(nestedfilter.getDepth());
    // WuNestedFilter.ScopeTypes
    StringArray & nestedScopeTypes = nestedfilter.getScopeTypes();
    ForEachItemIn(idx4,nestedScopeTypes)
        if (*nestedScopeTypes.item(idx4))
            wuScopeFilter.setIncludeScopeType(nestedScopeTypes.item(idx4));

    // WUAttributeToReturn.minVersion
    const char *minVersion = attribsToReturn.getMinVersion();
    if (minVersion && *minVersion)
    {
        StringBuffer sMinVersion("version[");
        sMinVersion.append(attribsToReturn.getMinVersion()).append("]");
        wuScopeFilter.addFilter(sMinVersion);
    }

    // WUAttributeToReturn.Measure
    wuScopeFilter.setMeasure(attribsToReturn.getMeasure());
    // WUAttributeToReturn.Attributes
    StringArray & attribs = attribsToReturn.getAttributes();
    ForEachItemIn(idx5,attribs)
        wuScopeFilter.addOutputAttribute(attribs.item(idx5));

    // TODO: WUAttributeToReturn.ScopeOverrides.scopeType
    // TODO: WUAttributeToReturn.ScopeOverrides.Attributes

    // WUScopeOptions.IncludeMatchedScopesInResults
    wuScopeFilter.setIncludeMatch(scopeOptions.getIncludeMatchedScopesInResults());
}
