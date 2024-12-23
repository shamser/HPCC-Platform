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


#include "platform.h"
#include "limits.h"
#include "slave.ipp"
#include "thactivityutil.ipp"
#include "jqueue.tpp"


#include "jexcept.hpp"
#include "jiface.hpp"
#include "jmisc.hpp"
#include "jio.hpp"
#include "jsem.hpp"
#include "eclhelper.hpp"
#include "thorxmlwrite.hpp"
#include "thexception.hpp"

#include "roxierow.hpp"
#include "thbufdef.hpp"

//#define TRACEROLLING

//#define TEST_PARALLEL_MATCH

#include "thsortu.hpp"

struct CRollingCacheElem
{
    int cmp;
    const void *row;
    CRollingCacheElem()
    {
        row = NULL;
        cmp = INT_MIN; 
    }
    ~CRollingCacheElem()
    {
        if (row)
            ReleaseThorRow(row);
    }
    void set(const void *_row)
    {
        if (row)
            ReleaseThorRow(row);
        row = _row;
    }
};


class CRollingCache: extends CInterface
{
    unsigned max; // max cache size
    QueueOf<CRollingCacheElem,true> cache;
    Linked<IRowStream> in;
    bool eos;
public:
    ~CRollingCache()
    {
        while (cache.ordinality())
        {
            CRollingCacheElem *e = cache.dequeue();
            delete e;
        }
    }

    void init(IRowStream *_in, unsigned _max)
    {
        max = _max;
        in.set(_in);
        cache.clear();
        cache.reserve(max);
        eos = false;
        while (cache.ordinality()<max/2)
            cache.enqueue(NULL);
        while (!eos&&(cache.ordinality()<max))
            advance();
#ifdef TRACEROLLING
        ActPrintLog("CRollingCache::initdone");
#endif
    }

#ifdef TRACEROLLING
    void PrintCache()
    {
        ActPrintLog("================================%s", eos?"EOS":"");
        for (unsigned i = 0;i<max;i++) {
            CRollingCacheElem *e = cache.item(i);
            ActPrintLog("%c%d: %s",(i==max/2)?'>':' ',i,e?(const char *)e->row():"-----");
        }
    }
#endif

    inline CRollingCacheElem *mid(int rel)
    {
        return cache.item(max/2+rel); // relies on unsigned wrap
    }

    void advance()
    {
        CRollingCacheElem *e = (cache.ordinality()==max)?cache.dequeue():NULL;
        if (!eos) {
            if (!e)
                e = new CRollingCacheElem();
            e->set(in->ungroupedNextRow());
            if (e->row) {
                cache.enqueue(e);
#ifdef TRACEROLLING
                PrintCache();
#endif
                return;
            }
            else
                eos = true;
        }
        delete e;
        cache.enqueue(NULL);
#ifdef TRACEROLLING
        PrintCache();
#endif
    }

};

class CDualCache: public CSimpleInterface
{
    // similar to rolling cache - should be combined really
    Linked<IRowStream> in;
    bool eos;
    unsigned base;
    unsigned pos1;
    unsigned pos2;
    QueueOf<CRollingCacheElem,true> cache;
public:
    CDualCache()
    {
        strm1 = NULL;
        strm2 = NULL;
    }

    ~CDualCache()
    {
        ::Release(strm1);
        ::Release(strm2);
        while (cache.ordinality())
        {
            CRollingCacheElem *e = cache.dequeue();
            delete e;
        }
    }


    void init(IRowStream *_in)
    {
        in.set(_in);
        cache.clear();
        eos = false;
#ifdef TRACEROLLING
        ActPrintLog("CDualCache::initdone");
#endif
        base = 0;
        pos1 = 0;
        pos2 = 0;
        strm1 = new cOut(this,pos1);
        strm2 = new cOut(this,pos2) ;
    }

#ifdef TRACEROLLING
    void PrintCache()
    {
        ActPrintLog("================================%s", eos?"EOS":"");
        for (unsigned i = 0;i<max;i++) {
            CRollingCacheElem *e = cache.item(i);
            ActPrintLog("%c%d: %s",(i==max/2)?'>':' ',i,e?(const char *)e->row():"-----");
        }
    }
#endif

    bool get(unsigned n, CRollingCacheElem *&out)
    {
        // take off any no longer needed
        CRollingCacheElem *e=NULL;
        while ((base<pos1)&&(base<pos2)) {
            delete e;
            e = cache.dequeue();
            base++;
        }
        assertex(n>=base);
        while (!eos&&(n-base>=cache.ordinality())) {
            if (!e)
                e = new CRollingCacheElem;
            e->set(in->ungroupedNextRow());
            if (!e->row) {
                eos = true;
                break;
            }
            cache.enqueue(e);
            e = NULL;
#ifdef TRACEROLLING
            PrintCache();
#endif
        }
        delete e;
        if (n-base>=cache.ordinality())
            return false;
        out = cache.item(n-base);
        return true;
    }

    class cOut: public IRowStream, public CSimpleInterface
    {
    private:
        CDualCache *parent;
        bool stopped;
        unsigned &pos;
    public:
        IMPLEMENT_IINTERFACE_USING(CSimpleInterface);
        cOut(CDualCache *_parent, unsigned &_pos) 
            : pos(_pos)
        {
            parent = _parent;
            stopped = false;
        }

        const void *nextRow()
        { 
            CRollingCacheElem *e;
            if (stopped||!parent->get(pos,e)) 
                return NULL;
            LinkThorRow(e->row);
            pos++;
            return e->row;
        }


        void stop()
        {
            pos = (unsigned)-1;
            stopped = true;
        }

    } *strm1, *strm2;

    IRowStream *queryOut1() { return strm1; }
    IRowStream *queryOut2() { return strm2; }
};



#define CATCH_MEMORY_EXCEPTIONS \
catch (IException *e) {     \
  IException *ne = MakeActivityException(&activity, e); \
  ::Release(e); \
  throw ne; \
}


void swapRows(RtlDynamicRowBuilder &row1, RtlDynamicRowBuilder &row2)
{
    row1.swapWith(row2);
}


class CJoinHelper : implements IJoinHelper, public CSimpleInterface
{
    CActivityBase &activity;
    IThorRowInterfaces *rowIf = nullptr;
    ICompare *compareLR = nullptr;
    ICompare *compareL = nullptr;
    ICompare *compareR = nullptr;

    CThorExpandingRowArray rightgroup;
    OwnedConstThorRow prevleft;
    OwnedConstThorRow prevright;            // used for first
    OwnedConstThorRow nextright;
    OwnedConstThorRow nextleft;
    OwnedConstThorRow denormLhs;
    RtlDynamicRowBuilder denormTmp;
    CThorExpandingRowArray denormRows;
    unsigned denormCount = 0;
    unsigned joinCounter = 0;
    size32_t outSz = 0;
    unsigned rightidx = 0;
    enum { JScompare, JSmatch, JSrightgrouponly, JSonfail } state = JScompare;
    bool eofL = false;
    bool eofR = false;
    IHThorJoinArg *helper = nullptr;
    IOutputMetaData * outputmetaL = nullptr;
    bool leftouter = false;
    bool rightouter = false;
    bool exclude = false;
    bool firstonlyL = false;
    bool firstonlyR = false;
    MemoryBuffer rightgroupmatchedbuf;
    bool *rightgroupmatched = nullptr;
    bool leftmatched = false;
    bool rightmatched = false;
    OwnedConstThorRow defaultLeft;
    OwnedConstThorRow defaultRight;
    Linked<IRowStream> strmL;
    Linked<IRowStream> strmR;
    JoinMatchStats matchStats;
    bool abort = false;
    bool nextleftgot = false;
    bool nextrightgot = false;
    unsigned atmost = (unsigned)-1;
    rowcount_t lhsProgressCount = 0, rhsProgressCount = 0;
    rowcount_t startMatchLhsProgressCount = 0;
    unsigned keepmax = (unsigned)-1;
    unsigned abortlimit = (unsigned)-1;
    unsigned keepremaining = (unsigned)-1;
    bool betweenjoin = false;
    Owned<IException> onFailException;
    ThorActivityKind kind = TAKnone;
    Owned<ILimitedCompareHelper> limitedhelper;
    Owned<CDualCache> dualcache;
    Linked<IEngineRowAllocator> allocator;
    IMulticoreIntercept* mcoreintercept = nullptr;
    Linked<IEngineRowAllocator> allocatorL;
    Linked<IEngineRowAllocator> allocatorR;

    struct cCompareLR: public ICompare
    {
        ICompare *upper;
        ICompare *lower;
        int docompare(const void *l,const void *r) const
        {
            int cmp = upper->docompare(l,r);
            if (cmp<=0) {
                cmp = lower->docompare(l,r);
                if (cmp>0)
                    cmp = 0;    // in range
            }
            return cmp;
        }
    } btwcompLR;

public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

    CJoinHelper(CActivityBase &_activity, IHThorJoinArg *_helper, IThorRowInterfaces *_rowIf)
        : activity(_activity), rowIf(_rowIf), denormTmp(NULL), rightgroup(_activity, NULL), denormRows(_activity, NULL), helper(_helper)
    {
        allocator.set(rowIf->queryRowAllocator());
        kind = activity.queryContainer().getKind();
    }

    ~CJoinHelper()
    {
        strmL.clear();
        strmR.clear();
        limitedhelper.clear();
    }

    bool init(
            IRowStream *_strmL,
            IRowStream *_strmR,
            IEngineRowAllocator *_allocatorL,
            IEngineRowAllocator *_allocatorR,
            IOutputMetaData * _outputmeta,
            IMulticoreIntercept *_mcoreintercept)
    {
        //DebugBreak();

        assertex(_allocatorL);
        assertex(_allocatorR);
        mcoreintercept = _mcoreintercept;
        eofL = false;
        eofR = false;
        if (!_strmR) { // (limited) self join
           dualcache.setown(new CDualCache());
           dualcache->init(_strmL);
           strmL.set(dualcache->queryOut1());
           strmR.set(dualcache->queryOut2());
        }
        else {
            strmL.set(_strmL);
            strmR.set(_strmR);
        }
        allocatorL.set(_allocatorL);
        allocatorR.set(_allocatorR);
        rightgroup.kill();
        rightmatched = false;
        state = JScompare;
        nextleftgot = false;
        nextrightgot = false;
        unsigned flags = helper->getJoinFlags();
        betweenjoin = (flags&JFslidingmatch)!=0;
        if (betweenjoin) {
            assertex(_strmR); // betweenjoin and limited not allowed!
            btwcompLR.lower = helper->queryCompareLeftRightLower();
            btwcompLR.upper = helper->queryCompareLeftRightUpper();
            compareLR = &btwcompLR;
        }
        else 
            compareLR = helper->queryCompareLeftRight();
        compareL = helper->queryCompareLeft();
        compareR = helper->queryCompareRight();
        if ((flags&JFlimitedprefixjoin)&&(helper->getJoinLimit())) {
            limitedhelper.setown(createLimitedCompareHelper());
            limitedhelper->init(helper->getJoinLimit(),strmR,compareLR,helper->queryPrefixCompare());
        }
        else
            limitedhelper.clear();
        leftouter = (flags & JFleftouter) != 0;
        rightouter = (flags & JFrightouter) != 0;
        exclude = (flags & JFexclude) != 0;
        firstonlyL = false;
        firstonlyR = false;
        if (flags & JFfirst) {   
            firstonlyL = true;
            firstonlyR = true;
            assertex(!leftouter);
            assertex(!rightouter);
        }
        else {
            if (flags & JFfirstleft) {
                assertex(!leftouter);
                firstonlyL = true;
            }
            if (flags & JFfirstright) {
                assertex(!rightouter);
                firstonlyR = true;
            }
        }
        if (rightouter) {
            RtlDynamicRowBuilder r(allocatorL);
            size32_t sz = helper->createDefaultLeft(r);
            defaultLeft.setown(r.finalizeRowClear(sz));
        }
        else
            defaultLeft.clear();
        if (leftouter || JFonfail & helper->getJoinFlags()) {
            RtlDynamicRowBuilder r(allocatorR);
            size32_t sz = helper->createDefaultRight(r);
            defaultRight.setown(r.finalizeRowClear(sz));
        }
        else
            defaultRight.clear();
        abort = false;
        atmost = helper->getJoinLimit();
        if (atmost)
            assertex(!rightouter);
        else
            atmost = (unsigned)-1;
        keepmax = helper->getKeepLimit();
        if (!keepmax)
            keepmax = (unsigned)-1;
        abortlimit = helper->getMatchAbortLimit();
        if (!abortlimit)
            abortlimit = (unsigned)-1;
        keepremaining = keepmax;
        outputmetaL = _outputmeta;
        if (TAKdenormalize == kind || TAKhashdenormalize == kind || TAKlookupdenormalize == kind || TAKsmartdenormalize == kind)
            denormTmp.setAllocator(allocator).ensureRow();
        denormCount = 0;
        joinCounter = 0;
        outSz = 0;
        rightidx = 0;
        leftmatched = false;
        lhsProgressCount = 0;
        rhsProgressCount = 0;
        return true;
    }

    bool getL()
    {
        if (nextleftgot)
            return true;
        keepremaining = keepmax;
        leftmatched = !leftouter;
        resetDenorm();
        prevleft.setown(nextleft.getClear());
        if (!eofL) {
            for (;;) {
                nextleft.setown(strmL->nextRow());
                if (!nextleft) 
                    break;

                lhsProgressCount++;
                if (!firstonlyL || (lhsProgressCount==1) || (compareL->docompare(prevleft,nextleft)!=0)) {
                    denormLhs.set(nextleft.get());
                    nextleftgot = true;
                    joinCounter = 0;
                    return true;
                }
            }
            eofL = true;
        }
        return false;
    }

    bool getR()
    {
        if (nextrightgot)
            return true;
        rightmatched = !rightouter;
        prevright.setown(nextright.getClear());
        if (!eofR) {
            for (;;) {
                nextright.setown(strmR->nextRow());
                if (!nextright) 
                    break;
                rhsProgressCount++;
                if (!firstonlyR || (rhsProgressCount==1) || (compareR->docompare(prevright,nextright)!=0)) {
                    nextrightgot = true;
                    return true;
                }
            }
            eofR = true;
        }
        return false;
    }

    inline void nextL()
    {
        nextleftgot = false;
    }

    inline void nextR()
    {
        nextrightgot = false;
    }

    enum Otype { Onext, Ogroup, Oouter };

    const void * outrow(Otype l,Otype r)
    {
        OwnedConstThorRow fret; // used if passing back a prefinalized row
        RtlDynamicRowBuilder ret(allocator);
        // this routine does quite a lot including advancing on outer
        assertex(l!=Ogroup);    // left group no longer present (asymmetry)
        size32_t gotsz = 0;
        bool denormGot = false;
        if (l==Oouter) {
            assertex(r!=Oouter);
            if (r==Onext) {
                if (!rightmatched) {
                    switch (kind) {
                        case TAKdenormalize:
                        case TAKhashdenormalize:
                        case TAKlookupdenormalize:
                        case TAKsmartdenormalize:
                        {
                            const void *lhs = defaultLeft;
                            do {
                                gotsz = helper->transform(denormTmp, lhs, nextright, ++denormCount, JTFmatchedright); //manufactured left
                                if (gotsz) {
                                    swapRows(denormTmp, ret);
                                    lhs = (const void *)ret.getSelf();
                                }
                                nextR();
                            }
                            while (getR()&&(0 == compareR->docompare(prevright,nextright)));
                            denormCount = 0;
                            break;
                        }
                        case TAKdenormalizegroup:
                        case TAKhashdenormalizegroup:
                        case TAKlookupdenormalizegroup:
                        case TAKsmartdenormalizegroup:
                            assertex(!denormRows.ordinality());
                            do {
                                denormRows.append(nextright.getLink());
                                nextR();
                            }
                            while (getR()&&(0 == compareR->docompare(prevright,nextright)));
                            gotsz = helper->transform(ret, defaultLeft, denormRows.query(0), denormRows.ordinality(), denormRows.getRowArray(), JTFmatchedright);
                            denormRows.kill();
                            break;
                        case TAKjoin:
                        case TAKhashjoin:
                        case TAKselfjoin:
                        case TAKselfjoinlight:
                        case TAKlookupjoin:
                        case TAKsmartjoin:
                            gotsz = helper->transform(ret, defaultLeft, nextright, 0, JTFmatchedright);
                            nextR();
                            break;
                        default:
                            throwUnexpected();
                    }
                }
                else
                    nextR();
            }
            else {
                switch (kind) {
                    case TAKdenormalize:
                    case TAKhashdenormalize:
                    case TAKlookupdenormalize:
                    case TAKsmartdenormalize:
                    {
                        const void *lhs = defaultLeft;
                        do {
                            if (!rightgroupmatched[rightidx]) {
                                gotsz = helper->transform(denormTmp, lhs, rightgroup.query(rightidx), ++denormCount, JTFmatchedright); //manufactured left
                                if (gotsz) {
                                    swapRows(denormTmp, ret);
                                    lhs = (const void *)ret.getSelf();
                                }
                            }
                            ++rightidx;
                        }
                        while (rightidx<rightgroup.ordinality());
                        denormCount = 0;
                        break;
                    }
                    case TAKdenormalizegroup:
                    case TAKhashdenormalizegroup:
                    case TAKlookupdenormalizegroup:
                    case TAKsmartdenormalizegroup:
                        assertex(!denormRows.ordinality());
                        do {
                            if (!rightgroupmatched[rightidx])
                                denormRows.append(rightgroup.getClear(rightidx));
                            ++rightidx;
                        }
                        while (rightidx<rightgroup.ordinality());
                        if (denormRows.ordinality())
                        {
                            gotsz = helper->transform(ret, defaultLeft, denormRows.query(0), denormRows.ordinality(), denormRows.getRowArray(), JTFmatchedright);
                            denormRows.kill();
                        }
                        denormCount = 0;
                        break;
                    case TAKjoin:
                    case TAKhashjoin:
                    case TAKselfjoin:
                    case TAKselfjoinlight:
                    case TAKlookupjoin:
                    case TAKsmartjoin:
                        if (!rightgroupmatched[rightidx]) 
                            gotsz = helper->transform(ret, defaultLeft, rightgroup.query(rightidx), 0, JTFmatchedright);
                        rightidx++;
                        break;
                    default:
                        throwUnexpected();
                }
            }
        }
        else if (r==Oouter) {
            if (!leftmatched) 
            {
                switch (kind) {
                    case TAKdenormalize:
                    case TAKhashdenormalize:
                    case TAKlookupdenormalize:
                    case TAKsmartdenormalize:
                        fret.set(nextleft);
                        break;
                    case TAKdenormalizegroup:
                    case TAKhashdenormalizegroup:
                    case TAKlookupdenormalizegroup:
                    case TAKsmartdenormalizegroup:
                        gotsz = helper->transform(ret, nextleft, defaultRight, 0, (const void **)NULL, JTFmatchedleft);
                        break;
                    case TAKjoin:
                    case TAKhashjoin:
                    case TAKselfjoin:
                    case TAKselfjoinlight:
                    case TAKlookupjoin:
                    case TAKsmartjoin:
                        gotsz = helper->transform(ret, nextleft, defaultRight, 0, JTFmatchedleft);
                        break;
                    default:
                        throwUnexpected();
                }
            }
            else
            {
                // output group if needed before advancing

                switch (kind)
                {
                    case TAKdenormalize:
                    case TAKhashdenormalize:
                    case TAKlookupdenormalize:
                    case TAKsmartdenormalize:
                        if (outSz)
                            fret.setown(denormLhs.getClear()); // denormLhs holding transform progress
                        break;
                    case TAKdenormalizegroup:
                    case TAKhashdenormalizegroup:
                    case TAKlookupdenormalizegroup:
                    case TAKsmartdenormalizegroup:
                        if (denormRows.ordinality())
                        {
                            gotsz = helper->transform(ret, nextleft, denormRows.query(0), denormRows.ordinality(), denormRows.getRowArray(), JTFmatchedleft|JTFmatchedright);
                            denormRows.kill();
                        }
                        break;
                    default:
                        break;
                }
            }
            nextL();            // output outer once
        }
        else { // not outer
            if (keepremaining==0)
                return NULL;                            //  treat KEEP expiring as match fail
            if (r==Onext) {
                // JCSMORE - I can't see when this can happen? if r==Onext, l is always Oouter.
                if (!exclude) 
                    gotsz = helper->transform(ret,nextleft,nextright,++joinCounter, JTFmatchedleft|JTFmatchedright);
                rightmatched = true;
            }
            else {
                if (!exclude)
                {
                    switch (kind) {
                        case TAKdenormalize:
                        case TAKhashdenormalize:
                        case TAKlookupdenormalize:
                        case TAKsmartdenormalize:
                        {
                            size32_t sz = helper->transform(ret, denormLhs, rightgroup.query(rightidx), ++denormCount, JTFmatchedleft|JTFmatchedright);
                            if (sz)
                            {
                                denormLhs.setown(ret.finalizeRowClear(sz));
                                outSz = sz; // have feeling could use denormGot and reset it.
                                denormGot = true;
                            }
                            break;
                        }
                        case TAKdenormalizegroup:
                        case TAKhashdenormalizegroup:
                        case TAKlookupdenormalizegroup:
                        case TAKsmartdenormalizegroup:
                        {
                            const void *rhsRow = rightgroup.query(rightidx);
                            LinkThorRow(rhsRow);
                            denormRows.append(rhsRow);
                            denormGot = true;
                            break;
                        }
                        case TAKjoin:
                        case TAKhashjoin:
                        case TAKselfjoin:
                        case TAKselfjoinlight:
                        case TAKlookupjoin:
                        case TAKsmartjoin:
                            gotsz = helper->transform(ret,nextleft,rightgroup.query(rightidx), ++joinCounter, JTFmatchedleft|JTFmatchedright);
                            break;
                        default:
                            throwUnexpected();
                    }
                }
                rightgroupmatched[rightidx] = true;
            }
            if ((gotsz||denormGot||fret.get())&&(keepremaining!=(unsigned)-1))
                keepremaining--;
            // treat SKIP and exclude as match success
            leftmatched = true;
        }
        if (gotsz)
            return ret.finalizeRowClear(gotsz);
        if (fret)
            return fret.getClear();
        return NULL;
    }
    void resetDenorm()
    {
        switch (kind)
        {
        case TAKdenormalizegroup:
        case TAKhashdenormalizegroup:
        case TAKlookupdenormalizegroup:
        case TAKsmartdenormalizegroup:
            denormRows.kill(); // fall through
        case TAKdenormalize:
        case TAKhashdenormalize:
        case TAKlookupdenormalize:
        case TAKsmartdenormalize:
            outSz = 0;
            denormLhs.clear();
            denormCount = 0;
        default:
            break;
        }
    }
    virtual const void *nextRow()
    {
        OwnedConstThorRow ret;
        RtlDynamicRowBuilder failret(allocator, false);
        try {
    retry:
            ret.clear();
            do {
                if (abort)
                    return NULL;
                switch (state) {
                case JSonfail:
                    do
                    {
                        size32_t transformedSize = helper->onFailTransform(failret.ensureRow(), nextleft, defaultRight, onFailException.get(), JTFmatchedleft);
                        nextL();
                        if (!getL()||0!=compareL->docompare(nextleft,prevleft))
                            state = JScompare;
                        if (transformedSize) {
                            if (mcoreintercept) {
                                mcoreintercept->addRow(failret.finalizeRowClear(transformedSize));
                                goto retry;
                            }
                            return failret.finalizeRowClear(transformedSize);
                        }
                    }
                    while (state == JSonfail);
                    //We have read a row that does not match, so decrement by 1 to get the count for the row that mismatched
                    {
                        //Nested scope to avoid problems with variable leaking into the following case
                        rowcount_t nextStartMatchLhsProgressCount = lhsProgressCount - 1;
                        matchStats.noteGroup(nextStartMatchLhsProgressCount - startMatchLhsProgressCount, 0);
                        startMatchLhsProgressCount = nextStartMatchLhsProgressCount;
                    }
                    // fall through
                case JScompare:                         
                    //Need to create a new match group when the right has been completely processed
                    if (getL()) {
                        rightidx = 0;
                        rightgroupmatched = NULL;
                        if (betweenjoin) {
                            unsigned nr = 0;
                            while ((nr<rightgroup.ordinality())&&(btwcompLR.upper->docompare(nextleft,rightgroup.query(nr))>0))
                                nr++;
                            rightgroup.removeRows(0,nr);
                            rightgroupmatched = (bool *)rightgroupmatchedbuf.clear().reserve(rightgroup.ordinality());
                            memset_iflen(rightgroupmatched,rightmatched?1:0,rightgroup.ordinality());
                        }
                        else
                            rightgroup.kill();

                        // now add new
                        bool hitatmost=false;
                        int cmp = -1;
                        // now load the right group
                        if (limitedhelper) {
                            limitedhelper->getGroup(rightgroup,nextleft);
                            if (rightgroup.ordinality()) {
                                state = JSmatch;
                                rightgroupmatched = (bool *)rightgroupmatchedbuf.clear().reserve(rightgroup.ordinality());
                                memset(rightgroupmatched,1,rightgroup.ordinality()); // no outer
                            }
                            else 
                                ret.setown(outrow(Onext,Oouter)); // out left outer and advance left
                        }
                        else {
                            while (getR()) {
                                cmp = compareLR->docompare(nextleft,nextright);
                                if (cmp!=0)
                                    break;
                                if (rightgroup.ordinality()==abortlimit) {
                                    if ((helper->getJoinFlags()&JFmatchAbortLimitSkips)==0) {
                                        try
                                        {
                                            helper->onMatchAbortLimitExceeded();
                                            CommonXmlWriter xmlwrite(0);
                                            if (outputmetaL && outputmetaL->hasXML()) {
                                                outputmetaL->toXML((const byte *) nextleft.get(), xmlwrite);
                                            }
                                            throw MakeStringException(0, "More than %d match candidates in join for row %s", abortlimit, xmlwrite.str());
                                        }
                                        catch (IException *_e)
                                        {
                                            if (0 == (JFonfail & helper->getJoinFlags()))
                                                throw;
                                            onFailException.setown(_e);
                                        }
                                        do nextR(); while( getR()&&(compareLR->docompare(nextleft,nextright)==0));
                                        state = JSonfail;
                                        goto retry;
                                    }
                                    do nextR(); while( getR()&&(compareLR->docompare(nextleft,nextright)==0));
                                    // discard lhs row(s) (yes, even if it is an outer join)
                                    do { 
                                        nextL();
                                    } while(getL()&&(compareL->docompare(nextleft,prevleft)==0));
                                    goto retry;
                                }
                                else if (rightgroup.ordinality()==atmost) {
                                    do nextR(); while( getR()&&(compareLR->docompare(nextleft,nextright)==0));
                                    hitatmost = true;
                                    cmp = -1;
                                    break;
                                }
                                rightgroup.append(nextright.getClear());
                                nextR();
                            }
                            rightgroupmatched = (bool *)rightgroupmatchedbuf.clear().reserve(rightgroup.ordinality());
                            memset_iflen(rightgroupmatched,rightmatched?1:0,rightgroup.ordinality());
                            if (!hitatmost&&rightgroup.ordinality())
                                state = JSmatch;
                            else if (cmp<0)
                            {
                                //Left row and no match right row
                                matchStats.noteGroup(1, 0); // This will not spot large left groups
                                startMatchLhsProgressCount = lhsProgressCount;
                                ret.setown(outrow(Onext,Oouter));
                            }
                            else 
                            {
                                //Right row with no matching left rows.
                                //This will not spot large right groups since it processes a row at a time
                                matchStats.noteGroup(0, 1);
                                ret.setown(outrow(Oouter,Onext));
                            }
                        }

                    }
                    else if (getR())
                    {
                        //We would miss tracking a very large trailing right group, but it is not worth
                        //the extra work to spot it
                        //FUTURE: if (!rightouter) we could return null and stop reading the rhs.
                        ret.setown(outrow(Oouter,Onext));
                    }
                    else
                        return NULL;
                    break;
                case JSmatch: // matching left to right group       
                    if (mcoreintercept) {
                        CThorExpandingRowArray leftgroup(activity, NULL);
                        while (getL()) {
                            if (leftgroup.ordinality()) {
                                int cmp = compareL->docompare(nextleft,leftgroup.query(leftgroup.ordinality()-1));
                                if (cmp!=0)
                                    break;
                            }
                            leftgroup.append(nextleft.getClear());
                            nextL();
                        }
                        mcoreintercept->addWork(&leftgroup,&rightgroup);
                        startMatchLhsProgressCount = (lhsProgressCount - 1); // Never used, but keep consistent with other cases
                        state = JScompare;
                    }
                    else if (rightidx<rightgroup.ordinality()) {
                        if (helper->match(nextleft,rightgroup.query(rightidx)))
                            ret.setown(outrow(Onext,Ogroup));
                        rightidx++;
                    }
                    else { // all done
                        ret.setown(outrow(Onext,Oouter));
                        rightidx = 0;
                        if (getL()) {
                            int cmp = compareL->docompare(nextleft,prevleft);
                            if (cmp>0)
                            {
                                //Finished processing this group -> gather the stats for the number of join candidates.
                                //lhsProgressCount is one higher than the the row count that follows the end of group
                                rowcount_t numLeftRows = (lhsProgressCount - 1) - startMatchLhsProgressCount;
                                matchStats.noteGroup(numLeftRows, rightgroup.ordinality());
                                startMatchLhsProgressCount = (lhsProgressCount - 1);
                                state = JSrightgrouponly;
                            }
                            else if (cmp<0) 
                            {
                                activity.logRow("prev: ", *allocatorL->queryOutputMeta(), prevleft);
                                activity.logRow("next: ", *allocatorL->queryOutputMeta(), nextleft);
                                throw MakeStringException(-1,"JOIN LHS not in sorted order");
                            }
                        }
                        else
                        {
                            //Finished processing this group -> gather the stats for the number of join candidates.
                            rowcount_t numLeftRows = lhsProgressCount - startMatchLhsProgressCount;
                            matchStats.noteGroup(numLeftRows, rightgroup.ordinality());
                            startMatchLhsProgressCount = lhsProgressCount;
                            state = JSrightgrouponly;
                        }
                    }
                    break;
                case JSrightgrouponly:
                    //FUTURE: Avoid walking the right group if it is an inner/left only join.
                    // right group
                    if (rightidx<rightgroup.ordinality())
                        ret.setown(outrow(Oouter,Ogroup));
                    else  // all done
                        state = JScompare;
                    break;
                }
                if (mcoreintercept&&ret.get()) 
                    mcoreintercept->addRow(ret.getClear());
                
            } while (!ret.get());

        }
        CATCH_MEMORY_EXCEPTIONS
        return ret.getClear();;
    }
    virtual void stop() { abort = true; }
    virtual rowcount_t getLhsProgress() const { return lhsProgressCount; }
    virtual rowcount_t getRhsProgress() const { return rhsProgressCount; }

    virtual void gatherStats(CRuntimeStatisticCollection & stats) const override
    {
        //Left and right progress could be added here.
        matchStats.gatherStats(stats);
    }
};

class SelfJoinHelper: implements IJoinHelper, public CSimpleInterface
{
    CActivityBase &activity;
    IThorRowInterfaces *rowIf = nullptr;
    ICompare *compare = nullptr;
    CThorExpandingRowArray curgroup;
    unsigned leftidx = 0;
    unsigned rightidx = 0;
    JoinMatchStats matchStats;
    bool leftmatched = false;
    MemoryBuffer rightmatchedbuf;
    bool *rightmatched = nullptr;
    enum { JSload, JSmatch, JSleftonly, JSrightonly, JSonfail } state = JSload;
    bool eof = false;
    IHThorJoinArg *helper = nullptr;
    IOutputMetaData * outputmetaL = nullptr;
    bool leftouter = false;
    bool rightouter = false;
    bool exclude = false;
    bool firstonlyL = false;
    bool firstonlyR = false;
    OwnedConstThorRow defaultLeft;
    OwnedConstThorRow defaultRight;
    Owned<IRowStream> strm;
    bool abort = false;
    unsigned atmost = (unsigned)-1;
    rowcount_t progressCount = 0;
    unsigned joinCounter = 0;
    unsigned keepmax = (unsigned)-1;
    unsigned abortlimit = (unsigned)-1;
    unsigned keepremaining = (unsigned)-1;
    OwnedConstThorRow nextrow;
    Owned<IException> onFailException;
    Linked<IEngineRowAllocator> allocator;
    Linked<IEngineRowAllocator> allocatorin;
    IMulticoreIntercept* mcoreintercept = nullptr;
    
public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

    SelfJoinHelper(CActivityBase &_activity, IHThorJoinArg *_helper, IThorRowInterfaces *_rowIf)
        : activity(_activity), rowIf(_rowIf), curgroup(_activity, &_activity)
    {
        allocator.set(rowIf->queryRowAllocator());
        helper = _helper;       
    }

    bool init(
            IRowStream *_strm,
            IRowStream *strmR,      // not used for self join - must be NULL
            IEngineRowAllocator *_allocatorL,
            IEngineRowAllocator *,
            IOutputMetaData * _outputmeta,
            IMulticoreIntercept *_mcoreintercept)
    {
        //DebugBreak();
        assertex(_allocatorL);
        mcoreintercept = _mcoreintercept;
        eof = false;
        strm.set(_strm);
        assertex(strmR==NULL);
        allocatorin.set(_allocatorL);
        state = JSload;
        unsigned flags = helper->getJoinFlags();
        assertex((flags&JFslidingmatch)==0);
        compare = helper->queryCompareLeft();
        leftouter = (flags & JFleftouter) != 0;
        rightouter = (flags & JFrightouter) != 0;
        exclude = (flags & JFexclude) != 0;
        firstonlyL = false; // I think first is depreciated but support anyway
        firstonlyR = false;
        if (flags & JFfirst) {    
            firstonlyL = true;
            firstonlyR = true;
            assertex(!leftouter);
            assertex(!rightouter);
        }
        else {
            if (flags & JFfirstleft) {
                assertex(!leftouter);
                firstonlyL = true;
            }
            if (flags & JFfirstright) {
                assertex(!rightouter);
                firstonlyR = true;
            }
        }
        if (rightouter) {
            RtlDynamicRowBuilder r(allocatorin);
            size32_t sz =helper->createDefaultLeft(r);
            defaultLeft.setown(r.finalizeRowClear(sz));
        }
        if (leftouter || JFonfail & helper->getJoinFlags()) {
            RtlDynamicRowBuilder r(allocatorin);
            size32_t sz = helper->createDefaultRight(r);
            defaultRight.setown(r.finalizeRowClear(sz));
        }
        abort = false;
        atmost = helper->getJoinLimit();
        if (atmost)
            assertex(!rightouter);
        else
            atmost = (unsigned)-1;
        keepmax = helper->getKeepLimit();
        if (!keepmax)
            keepmax = (unsigned)-1;
        abortlimit = helper->getMatchAbortLimit();
        if (!abortlimit)
            abortlimit = (unsigned)-1;
        keepremaining = keepmax;
        outputmetaL = _outputmeta;
        progressCount = 0;
        joinCounter = 0;
        return true;
    }

    inline bool getRow()
    {
        if (!nextrow) {
            nextrow.setown(strm->nextRow());
            if (!nextrow)
                return false;
            progressCount++;
        }
        return true;
    }

    inline void next()
    {
        nextrow.clear();
    }

    virtual const void *nextRow()
    {
        OwnedConstThorRow ret;
        RtlDynamicRowBuilder failret(allocator, false);
        try {
retry:
            ret.clear();
            do {
                if (abort)
                    return NULL;
                switch (state) {
                case JSonfail:
                    if (leftidx<curgroup.ordinality()) {
                        size32_t transformedSize = helper->onFailTransform(failret.ensureRow(), curgroup.query(leftidx), defaultRight, onFailException.get(), JTFmatchedleft);
                        leftidx++;
                        if (transformedSize) {
                            if (mcoreintercept) {
                                mcoreintercept->addRow(failret.finalizeRowClear(transformedSize));
                                goto retry;
                            }
                            return failret.finalizeRowClear(transformedSize);
                        }
                        break;
                    }
                    else if (getRow() && (compare->docompare(nextrow,curgroup.query(0))==0)) {
                        size32_t transformedSize = helper->onFailTransform(failret, nextrow, defaultRight, onFailException.get(), JTFmatchedleft);
                        next();
                        if (transformedSize) {
                            if (mcoreintercept) {
                                mcoreintercept->addRow(failret.finalizeRowClear(transformedSize));
                                goto retry;
                            }
                            ret.setown(failret.finalizeRowClear(transformedSize));
                        }
                        break;
                    }
                    else  // all done
                        state = JSload;
                    // fall through
                case JSload:                            
                    // fill group
                    curgroup.kill();
                    rightmatchedbuf.clear();
                    rightmatched = NULL;
                    leftmatched = false;
                    keepremaining = keepmax;
                    if (eof) 
                        return NULL;
                    unsigned ng;
                    while (getRow()&&(((ng=curgroup.ordinality())==0)||(compare->docompare(nextrow,curgroup.query(0))==0))) {
                        if ((ng==abortlimit)||(ng==atmost)) {
                            if ((ng==abortlimit)&&((helper->getJoinFlags()&JFmatchAbortLimitSkips)==0)) {
                                // abort
                                try
                                {
                                    helper->onMatchAbortLimitExceeded();
                                    CommonXmlWriter xmlwrite(0);
                                    if (outputmetaL && outputmetaL->hasXML()) {
                                        outputmetaL->toXML((const byte *) nextrow.get(), xmlwrite);
                                    }
                                    throw MakeStringException(0, "More than %d match candidates in join for row %s", abortlimit, xmlwrite.str());
                                }
                                catch (IException *_e)
                                {
                                    if (0 == (JFonfail & helper->getJoinFlags()))
                                    {
                                        curgroup.kill();
                                        throw;
                                    }
                                    onFailException.setown(_e);
                                }
                                state = JSonfail;
                                break;
                            }
                            if ((ng!=abortlimit)&&leftouter) {
                                state = JSleftonly ; // rest get done as left outer 
                                break;
                            }
                            // throw away group
                            do { // skip group
                                next();
                            } while (getRow() && (compare->docompare(nextrow,curgroup.query(0))==0));
                            curgroup.kill();
                            rightmatchedbuf.clear();
                            eof = !nextrow.get();
                            goto retry;
                        }
                        if (!firstonlyR||!firstonlyL||(ng==0)) 
                            curgroup.append(nextrow.getClear());
                        next();
                    }
                    if (curgroup.ordinality()==0) {
                        eof = 0;
                        return NULL;
                    }
                    matchStats.noteGroup(curgroup.ordinality(), curgroup.ordinality());
                    if (curgroup.ordinality() > INITIAL_SELFJOIN_MATCH_WARNING_LEVEL) {
                        Owned<IThorException> e = MakeActivityWarning(&activity, TE_SelfJoinMatchWarning, "Exceeded initial match limit");
                        e->queryData().append((unsigned)curgroup.ordinality());
                        activity.fireException(e);
                    }
                    leftidx = 0;
                    rightidx = 0;
                    joinCounter = 0;
                    leftmatched = false;
                    if (state==JSload) {     // catch atmost above
                        rightmatched = (bool *)rightmatchedbuf.clear().reserve(curgroup.ordinality());
                        memset(rightmatched,rightouter?0:1,curgroup.ordinality());
                        state = JSmatch; // ok we have group so match
                    }
                    break;
                case JSmatch: {
                        const void *l = curgroup.query(leftidx); // leftidx should be in range here
                        if (mcoreintercept) {
                            mcoreintercept->addWork(&curgroup,NULL);
                            state = JSload;
                        }
                        else if ((rightidx<curgroup.ordinality())&&(!firstonlyR||(rightidx==0))) {
                            const void *r = curgroup.query(rightidx);
                            if (helper->match(l,r)) {
                                if (keepremaining>0) {
                                    if (!exclude) {
                                        RtlDynamicRowBuilder rtmp(allocator);
                                        size32_t sz = helper->transform(rtmp,l,r,++joinCounter, JTFmatchedleft|JTFmatchedright);
                                        if (sz)
                                            ret.setown(rtmp.finalizeRowClear(sz));
                                    }
                                    if (ret.get()&&(keepremaining!=(unsigned)-1))
                                        keepremaining--;
                                    // treat SKIP and exclude as match success
                                    if (rightouter)
                                        rightmatched[rightidx] = true;
                                    leftmatched = true;
                                }
                                else
                                    rightidx = curgroup.ordinality()-1;
                            }
                            rightidx++;
                        }
                        else { // right all done
                            if (leftouter&&!leftmatched) {
                                RtlDynamicRowBuilder rtmp(allocator);
                                size32_t sz = helper->transform(rtmp, l, defaultRight, 0, JTFmatchedleft);
                                if (sz)
                                    ret.setown(rtmp.finalizeRowClear(sz));
                            }
                            keepremaining = keepmax; // lefts don't count in keep
                            rightidx = 0;
                            joinCounter = 0;
                            leftidx++;
                            if ((leftidx>=curgroup.ordinality())||(firstonlyL&&(leftidx>0)))
                                state = JSrightonly;
                            else
                                leftmatched = false;
                        }
                    }
                    break;
                case JSleftonly: 
                    // must be left outer after atmost to get here
                    if (leftidx<curgroup.ordinality()) {
                        RtlDynamicRowBuilder rtmp(allocator);
                        size32_t sz = helper->transform(rtmp, curgroup.query(leftidx), defaultRight, 0, JTFmatchedleft);
                        if (sz)
                            ret.setown(rtmp.finalizeRowClear(sz));
                        leftidx++;
                    }
                    else if (getRow() && (compare->docompare(nextrow,curgroup.query(0))==0)) {
                        RtlDynamicRowBuilder rtmp(allocator);
                        size32_t sz = helper->transform(rtmp, nextrow, defaultRight, 0, JTFmatchedleft);
                        if (sz)
                            ret.setown(rtmp.finalizeRowClear(sz));
                        next();
                    }
                    else  // all done
                        state = JSload;
                    break;
                case JSrightonly: 
                    // right group
                    if (rightouter&&(rightidx<curgroup.ordinality())) {
                        if (!rightmatched[rightidx]) {
                            RtlDynamicRowBuilder rtmp(allocator);
                            size32_t sz = helper->transform(rtmp, defaultLeft,curgroup.query(rightidx), 0, JTFmatchedright);
                            if (sz)
                                ret.setown(rtmp.finalizeRowClear(sz));
                        }
                        rightidx++;
                    }
                    else  // all done
                        state = JSload;
                    break;
                }
                if (mcoreintercept&&ret.get()) 
                    mcoreintercept->addRow(ret.getClear());
            } while (!ret.get());

        }
        CATCH_MEMORY_EXCEPTIONS
        return ret.getClear();
    }
    virtual void stop() { abort = true; }
    virtual rowcount_t getLhsProgress() const { return progressCount; }
    virtual rowcount_t getRhsProgress() const { return progressCount; }
    virtual void gatherStats(CRuntimeStatisticCollection & stats) const override
    {
        //Left and right progress could be added here.
        matchStats.gatherStats(stats);
    }

};

IJoinHelper *createDenormalizeHelper(CActivityBase &activity, IHThorDenormalizeArg *helper, IThorRowInterfaces *rowIf)
{
    return new CJoinHelper(activity, helper, rowIf);
}









inline int iabs(int a) { return a<0?-a:a; }
inline int imin(int a,int b) { return a<b?a:b; }

class CLimitedCompareHelper: implements ILimitedCompareHelper, public CSimpleInterface
{

    Owned<CRollingCache> cache;
    unsigned atmost;
    ICompare * limitedcmp;
    ICompare * cmp;

public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

    void init( unsigned _atmost,
               IRowStream *_in,
               ICompare * _cmp,
               ICompare * _limitedcmp)
    {
        atmost = _atmost;
        cache.setown(new CRollingCache());
        cache->init(_in,(atmost+1)*2);
        cmp = _cmp;
        limitedcmp = _limitedcmp;
    }


    bool getGroup(CThorExpandingRowArray &group,const void *left)
    {
        // this could be improved!


        // first move 'mid' forwards until mid>=left
        int low = 0;
        for (;;) {
            CRollingCacheElem * r = cache->mid(0);
            if (!r)
                break; // hit eos
            int c = cmp->docompare(left,r->row);
            if (c==0) {
                r->cmp = limitedcmp->docompare(left,r->row);
                if (r->cmp<=0) 
                    break;
            }
            else if (c<0) {
                r->cmp = -1;
                break;
            }
            else
                r->cmp = 1;
            cache->advance();
            if (cache->mid(low-1))  // only if haven't hit start
                low--;
        }
        // now scan back (note low should be filled even at eos)
        for (;;) {
            CRollingCacheElem * pr = cache->mid(low-1);
            if (!pr)
                break; // hit start 
            int c = cmp->docompare(left,pr->row);
            if (c==0) {
                pr->cmp = limitedcmp->docompare(left,pr->row);
                if (pr->cmp==1) 
                    break;
            }
            else {
                pr->cmp = 1;  
                break;
            }
            low--;
        }
        int high = 0;
        if (cache->mid(0)) { // check haven't already hit end
            // now scan fwd
            for (;;) {
                high++;
                CRollingCacheElem * nr = cache->mid(high);
                if (!nr) 
                    break;
                int c = cmp->docompare(left,nr->row);
                if (c==0) {
                    nr->cmp = limitedcmp->docompare(left,nr->row);
                    if (nr->cmp==-1) 
                        break;
                }
                else {
                    nr->cmp = -1;
                    break;
                }
            }
        }
        while (high-low>((int)atmost)) {
            int vl = iabs(cache->mid(low)->cmp);
            int vh = iabs(cache->mid(high-1)->cmp);
            int v;
            if (vl==0) {
                if (vh==0)  // both ends equal
                    return false;
                v = vh;
            }
            else if (vh==0)
                v = vl;
            else
                v = imin(vl,vh);
            // remove worst match from either end
            while ((low<high)&&(iabs(cache->mid(low)->cmp)==v))
                low++;
            while ((low<high)&&(iabs(cache->mid(high-1)->cmp)==v))
                high--;
            if (low>=high) 
                return true;   // couldn't make group;
        }
        for (int i=low;i<high;i++) {
            CRollingCacheElem *r = cache->mid(i);
            LinkThorRow(r->row);
            group.append(r->row);   
        }
        return group.ordinality()>0;
    }
};


ILimitedCompareHelper *createLimitedCompareHelper()
{
    return new CLimitedCompareHelper();
}

//===============================================================

class CMultiCoreJoinHelperBase: implements IJoinHelper, implements IMulticoreIntercept, public CSimpleInterface
{
public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

    CActivityBase &activity;
    IThorRowInterfaces *rowIf;
    IJoinHelper *jhelper;
    bool leftouter;  
    bool rightouter;  
    bool exclude; 
    
    IHThorJoinArg *helper;  
    Linked<IEngineRowAllocator> allocator;
    OwnedConstThorRow defaultLeft;
    OwnedConstThorRow defaultRight;
    unsigned numworkers;
    ThorActivityKind kind;
    Owned<IException> exc;
    CriticalSection sect;
    bool eos, selfJoin;
    JoinMatchStats matchStats;

    void setException(IException *e,const char *title)
    {
        DBGLOG(e,title);
        CriticalBlock b(sect);
        if (exc.get())
            e->Release();
        else
            exc.setown(e);
    }


    class cWorkItem
    {
    public:
        CThorExpandingRowArray lgroup;
        CThorExpandingRowArray rgroup;
        const void *row;
        inline cWorkItem(CActivityBase &_activity, CThorExpandingRowArray *_lgroup, CThorExpandingRowArray *_rgroup)
            : lgroup(_activity, &_activity), rgroup(_activity, &_activity)
        {
            set(_lgroup,_rgroup);
        }
        inline cWorkItem(CActivityBase &_activity) : lgroup(_activity, &_activity), rgroup(_activity, &_activity)
        {
            clear();
        }

        inline void set(CThorExpandingRowArray *_lgroup, CThorExpandingRowArray *_rgroup)
        {
            if (_lgroup)
                lgroup.transfer(*_lgroup);
            else
                lgroup.kill();
            if (_rgroup)
                rgroup.transfer(*_rgroup);
            else
                rgroup.kill();
            row = NULL;
        }
        inline void set(const void *_row)
        {
            lgroup.kill();
            rgroup.kill();
            row = _row;
        }
        inline void clear()
        {
            set(NULL);
        }
    };

    void doMatch(cWorkItem &work, IRowWriter &writer, IEngineRowAllocator *theAllocator)
    {
        MemoryBuffer rmatchedbuf;  
        CThorExpandingRowArray &rgroup = selfJoin?work.lgroup:work.rgroup;
        bool *rmatched = NULL;
        if (rightouter) {
            rmatched = (bool *)rmatchedbuf.clear().reserve(rgroup.ordinality());
            memset_iflen(rmatched,0,rgroup.ordinality());
        }
        ForEachItemIn(leftidx,work.lgroup)
        {
            bool lmatched = !leftouter;
            unsigned joinCounter = 0;
            for (unsigned rightidx=0; rightidx<rgroup.ordinality(); rightidx++) {
                if (helper->match(work.lgroup.query(leftidx),rgroup.query(rightidx))) {
                    lmatched = true;
                    if (rightouter)
                        rmatched[rightidx] = true;
                    RtlDynamicRowBuilder ret(theAllocator);
                    size32_t sz = exclude?0:helper->transform(ret,work.lgroup.query(leftidx),rgroup.query(rightidx),++joinCounter,JTFmatchedleft|JTFmatchedright);
                    if (sz)
                        writer.putRow(ret.finalizeRowClear(sz));

                }
            }
            if (!lmatched) {
                RtlDynamicRowBuilder ret(theAllocator);
                size32_t sz =  helper->transform(ret, work.lgroup.query(leftidx), defaultRight, 0, JTFmatchedleft);
                if (sz)
                    writer.putRow(ret.finalizeRowClear(sz));
            }
        }
        if (rightouter) {
            ForEachItemIn(rightidx2,rgroup) {
                if (!rmatched[rightidx2]) {
                    RtlDynamicRowBuilder ret(theAllocator);
                    size32_t sz =  helper->transform(ret, defaultLeft, rgroup.query(rightidx2), 0, JTFmatchedright);
                    if (sz)
                        writer.putRow(ret.finalizeRowClear(sz));
                }
            }
        }
    }

    void noteGroupSizes(CThorExpandingRowArray *lgroup,CThorExpandingRowArray *rgroup)
    {
        rowidx_t numLeft = lgroup ? lgroup->ordinality() : 0;
        rowidx_t numRight = lgroup ? lgroup->ordinality() : 0;
        matchStats.noteGroup(numLeft, numRight);
    }

    virtual void gatherStats(CRuntimeStatisticCollection & stats) const override
    {
        matchStats.gatherStats(stats);
    }

    CMultiCoreJoinHelperBase(CActivityBase &_activity, unsigned numthreads, bool _selfJoin, IJoinHelper *_jhelper, IHThorJoinArg *_helper, IThorRowInterfaces *_rowIf)
        : activity(_activity), rowIf(_rowIf)
    {
        allocator.set(rowIf->queryRowAllocator());
        kind = activity.queryContainer().getKind();
        jhelper = _jhelper;
        helper = _helper;
        unsigned flags = helper->getJoinFlags();
        leftouter = (flags & JFleftouter) != 0;
        rightouter = (flags & JFrightouter) != 0;
        exclude = (flags & JFexclude) != 0;
        selfJoin = _selfJoin;
        numworkers = numthreads;
        eos = false;
    }

    bool init(
            IRowStream *strmL,
            IRowStream *strmR,      // not used for self join - must be NULL
            IEngineRowAllocator *allocatorL,
            IEngineRowAllocator *allocatorR,
            IOutputMetaData * outputmetaL,   // for XML output 
            IMulticoreIntercept *_mcoreintercept
        )
    {
        if (!jhelper->init(strmL,strmR,allocatorL,allocatorR,outputmetaL,this))
            return false;
        if (rightouter) {
            RtlDynamicRowBuilder r(allocatorL);
            size32_t sz = helper->createDefaultLeft(r);
            defaultLeft.setown(r.finalizeRowClear(sz));
        }
        if (leftouter) {
            RtlDynamicRowBuilder r(allocatorR);
            size32_t sz = helper->createDefaultRight(r);
            defaultRight.setown(r.finalizeRowClear(sz));
        }
        return true;
    }

// IJoinHelper impl.
    virtual rowcount_t getLhsProgress() const
    {
        if (jhelper)
            return jhelper->getLhsProgress();
        return 0;
    }
    virtual rowcount_t getRhsProgress() const
    {
        if (jhelper)
            return jhelper->getRhsProgress();
        return 0;
    }
    virtual void stop()
    {
        jhelper->stop();
    }
};


class CMultiCoreJoinHelper: public CMultiCoreJoinHelperBase
{
    unsigned curin;         // only updated from cReader thread
    unsigned curout;            // only updated from cReader thread
    bool stopped;
    
    class cReader: public Thread
    {
    public:
        CMultiCoreJoinHelper *parent;
        cReader()
            : Thread("CMultiCoreJoinHelper::cReader")
        {
        }
        int run()
        {
            LOG(MCthorDetailedDebugInfo, "CMultiCoreJoinHelper::cReader started");
            try {
                const void * row = parent->jhelper->nextRow();
                assertex(!row);
            }
            catch (IException *e) {
                parent->setException(e,"CMultiCoreJoinHelper::cReader");
            }
            for (unsigned i=0;i<parent->numworkers;i++) 
                parent->addWork(NULL,NULL);
            LOG(MCthorDetailedDebugInfo, "CMultiCoreJoinHelper::cReader exit");
            return 0;
        }
    } reader;

    class cWorker: public Thread
    {
        CMultiCoreJoinHelper *parent;
    public:
        cWorkItem work;
        Semaphore workready;
        Semaphore workwait;
        Owned<ISmartRowBuffer> rowStream;
        bool stopped;

        cWorker(CActivityBase &activity, CMultiCoreJoinHelper *_parent)
            : Thread("CMultiCoreJoinHelper::cWorker"), parent(_parent), work(activity)
        {
            stopped = false;
            // JCSMORE - could have a spilling variety instead here..
            rowStream.setown(createSmartInMemoryBuffer(&parent->activity, parent->rowIf, SMALL_SMART_BUFFER_SIZE));
        }
        int run()
        {
            LOG(MCthorDetailedDebugInfo, "CMultiCoreJoinHelper::cWorker started");

            Owned<IThorRowInterfaces> rowIf = parent->activity.getRowInterfaces();
            Owned<IEngineRowAllocator> allocator = parent->activity.getRowAllocator(rowIf->queryRowMetaData(), (roxiemem::RHFpacked|roxiemem::RHFunique));

            IRowWriter *rowWriter = rowStream->queryWriter();
            for (;;)
            {
                work.clear();
                workready.signal();
                workwait.wait();
                try
                {
                    if (work.row)
                        rowWriter->putRow(work.row);
                    else
                    {
                        if (work.lgroup.ordinality()==0)
                            break;
                        parent->doMatch(work, *rowWriter, allocator);
                    }
                    rowWriter->putRow(NULL);
                }
                catch (IException *e)
                {
                    parent->setException(e,"CMultiCoreJoinHelper::cWorker");
                    break;
                }
            }
            // reader will check stopped, which will only be set once all read, becasue flush() will block until all read
            rowWriter->putRow(NULL); // end-of-stream
            rowWriter->flush();
            stopped = true; // NB: will not get past flush(), until all read
            LOG(MCthorDetailedDebugInfo, "CMultiCoreJoinHelper::cWorker exit");
            return 0;
        }
        bool isStopped() const { return stopped; }
    } **workers;

public:
    CMultiCoreJoinHelper(CActivityBase &activity, unsigned numthreads, bool selfJoin, IJoinHelper *_jhelper, IHThorJoinArg *_helper, IThorRowInterfaces *_rowIf)
        : CMultiCoreJoinHelperBase(activity, numthreads, selfJoin, _jhelper, _helper, _rowIf)
    {
        reader.parent = this;
        stopped = false;
        workers = new cWorker *[numthreads];
        curin = 0;
        curout = 0;
        for (unsigned i=0;i<numthreads;i++)
            workers[i] = new cWorker(activity, this);
    }

    ~CMultiCoreJoinHelper()
    {
        stop(); // if hasn't been already
        if (!reader.join(1000*60))
            IERRLOG("~CMultiCoreJoinHelper reader join timed out");
        for (unsigned i=0;i<numworkers;i++) {
            if (!workers[i]->join(1000*60))
                IERRLOG("~CMultiCoreJoinHelper worker[%d] join timed out",i);
        }
        for (unsigned i=0;i<numworkers;i++) 
            delete workers[i];
        delete [] workers;
        ::Release(jhelper);
    }

// IJoinHelper impl.
    virtual bool init(
            IRowStream *strmL,
            IRowStream *strmR,      // not used for self join - must be NULL
            IEngineRowAllocator *allocatorL,
            IEngineRowAllocator *allocatorR,
            IOutputMetaData * outputmetaL,   // for XML output 
            IMulticoreIntercept *_mcoreintercept
        )
    {
        if (!CMultiCoreJoinHelperBase::init(strmL,strmR,allocatorL,allocatorR,outputmetaL,this))
            return false;
        stopped = false;
        for (unsigned i=0;i<numworkers;i++)
            workers[i]->start(true);
        reader.start(true);
        return true;
    }
    virtual const void *nextRow()
    {
        /* NB: workers fill and block, they output NULL at end of group
         * which signals this to move onto next worker.
         * When all work has been added to workers, a end work item marker is added to the workqueue
         * which causes the workers to finish, mark stopped and flush their stream.
         */
        for (;;)
        {
            if (eos)
                return NULL;
            const void *row = workers[curout]->rowStream->nextRow();
            if (exc.get())
            {
                ::ReleaseThorRow(row);
                CriticalBlock b(sect);
                throw exc.getClear();
            }
            if (row)
                return row;
            else
            {
                if (workers[curout]->isStopped())
                {
                    eos = true;
                    return NULL;
                }
            }
            curout = (curout+1)%numworkers;
        }
    }
    virtual void stop()
    {
        if (stopped)
            return;
        stopped = true;
        CMultiCoreJoinHelperBase::stop();
        for (unsigned i=0;i<numworkers;i++)
            workers[i]->rowStream->stop();
    }

// IMulticoreIntercept impl.
    virtual void addWork(CThorExpandingRowArray *lgroup,CThorExpandingRowArray *rgroup)
    {
        /* NB: This adds a work item to each worker in sequence
         * The pull side, also pulls from the workers in sequence
         * This ensures the output is return in input order.
         */
        noteGroupSizes(lgroup, rgroup);

        cWorker *worker = workers[curin];
        worker->workready.wait();
        workers[curin]->work.set(lgroup,rgroup);
        worker->workwait.signal();
        curin = (curin+1)%numworkers;
    }
    virtual void addRow(const void *row)
    {
        cWorker *worker = workers[curin];
        worker->workready.wait();
        workers[curin]->work.set(row);
        worker->workwait.signal();
        curin = (curin+1)%numworkers;
    }
};


class CMultiCoreUnorderedJoinHelper: public CMultiCoreJoinHelperBase
{
    void setException(IException *e,const char *title)
    {
        DBGLOG(e,title);
        CriticalBlock b(sect);
        if (exc.get())
            e->Release();
        else
            exc.setown(e);
        multiWriter->abort();
    }

    ReallySimpleInterThreadQueueOf<cWorkItem,false> workqueue;
    Owned<IRowMultiWriterReader> multiWriter;
    Owned<IRowWriter> rowWriter;

    class cReader: public Thread
    {
    public:
        CMultiCoreUnorderedJoinHelper *parent;
        cReader()
            : Thread("CMulticoreUnorderedJoinHelper::cReader")
        {
        }
        int run()
        {
            LOG(MCthorDetailedDebugInfo, "CMulticoreUnorderedJoinHelper::cReader started");
            try {
                const void * row = parent->jhelper->nextRow();
                assertex(!row);
            }
            catch (IException *e) {
                parent->setException(e,"CMulticoreUnorderedJoinHelper::cReader");
            }
            parent->stopWorkers();
            LOG(MCthorDetailedDebugInfo, "CMulticoreUnorderedJoinHelper::cReader exit");
            return 0;
        }
    } reader;

    class cWorker: public Thread
    {
        CMultiCoreUnorderedJoinHelper *parent;
    public:
        cWorker(CMultiCoreUnorderedJoinHelper *_parent)
            : Thread("CMulticoreUnorderedJoinHelper::cWorker"), parent(_parent)
        {
        }
        int run()
        {
            Owned<IThorRowInterfaces> rowIf = parent->activity.getRowInterfaces();
            Owned<IEngineRowAllocator> allocator = parent->activity.getRowAllocator(rowIf->queryRowMetaData(), (roxiemem::RHFpacked|roxiemem::RHFunique));

            Owned<IRowWriter> rowWriter = parent->multiWriter->getWriter();
            LOG(MCthorDetailedDebugInfo, "CMulticoreUnorderedJoinHelper::cWorker started");
            for (;;)
            {
                cWorkItem *work = parent->workqueue.dequeue();
                if (!work||((work->lgroup.ordinality()==0)&&(work->rgroup.ordinality()==0)))
                {
                    delete work;
                    break;
                }
                try
                {
                    parent->doMatch(*work, *rowWriter, allocator);
                    delete work;
                }
                catch (IException *e)
                {
                    parent->setException(e,"CMulticoreUnorderedJoinHelper::cWorker");
                    delete work;
                    break;
                }
            }
            LOG(MCthorDetailedDebugInfo, "CMulticoreUnorderedJoinHelper::cWorker exit");
            return 0;
        }
    } **workers;

public:
    CMultiCoreUnorderedJoinHelper(CActivityBase &activity, unsigned numthreads, bool selfJoin, IJoinHelper *_jhelper, IHThorJoinArg *_helper, IThorRowInterfaces *_rowIf)
        : CMultiCoreJoinHelperBase(activity, numthreads, selfJoin, _jhelper, _helper, _rowIf)
    {
        reader.parent = this;

        unsigned limit = activity.queryContainer().queryXGMML().getPropInt("hint[@name=\"limit\"]/@value", numworkers*1000);
        unsigned readGranularity = activity.queryContainer().queryXGMML().getPropInt("hint[@name=\"readgranularity\"]/@value", DEFAULT_WR_READ_GRANULARITY);
        unsigned writeGranularity = activity.queryContainer().queryXGMML().getPropInt("hint[@name=\"writegranularity\"]/@value", DEFAULT_WR_WRITE_GRANULARITY);
        ActPrintLog(&activity, thorDetailedLogLevel, "CMultiCoreUnorderedJoinHelper: limit=%d, readGranularity=%d, writeGranularity=%d", limit, readGranularity, writeGranularity);
        multiWriter.setown(createSharedWriteBuffer(&activity, rowIf, limit, readGranularity, writeGranularity));
        workers = new cWorker *[numthreads];
        for (unsigned i=0;i<numthreads;i++) 
            workers[i] = new cWorker(this);
    }

    ~CMultiCoreUnorderedJoinHelper()
    {
        stop();
        for (unsigned i=0;i<numworkers;i++) 
            delete workers[i];
        delete [] workers;
        ::Release(jhelper);
    }
    void stopWorkers()
    {
        for (unsigned i=0;i<numworkers;i++)
            addWork(NULL, NULL);
        rowWriter.clear();
    }

// IJoinHelper impl.
    virtual bool init(
            IRowStream *strmL,
            IRowStream *strmR,      // not used for self join - must be NULL
            IEngineRowAllocator *allocatorL,
            IEngineRowAllocator *allocatorR,
            IOutputMetaData * outputmetaL,   // for XML output 
            IMulticoreIntercept *_mcoreintercept
        )
    {
        if (!CMultiCoreJoinHelperBase::init(strmL,strmR,allocatorL,allocatorR,outputmetaL,this))
            return false;
        workqueue.setLimit(numworkers+1);
        rowWriter.setown(multiWriter->getWriter());
        for (unsigned i=0;i<numworkers;i++)
            workers[i]->start(true);
        reader.start(true);
        return true;
    }
    virtual const void *nextRow()
    {
        if (eos)
            return NULL;
        const void *ret = multiWriter->nextRow();
        if (exc.get())
        {
            ::ReleaseThorRow(ret);
            CriticalBlock b(sect);
            throw exc.getClear();
        }
        if (ret)
            return ret;
        eos = true;
        return NULL;
    }
    virtual void stop()
    {
        CMultiCoreJoinHelperBase::stop();
        workqueue.stop(numworkers, 1);
        multiWriter->abort();
        if (!reader.join(1000*60))
            IERRLOG("~CMulticoreUnorderedJoinHelper reader join timed out");
        for (unsigned i=0;i<numworkers;i++)
        {
            if (!workers[i]->join(1000*60))
                IERRLOG("~CMulticoreUnorderedJoinHelper worker[%d] join timed out",i);
        }
        while (workqueue.ordinality())
            delete workqueue.dequeueNow();
    }

// IMulticoreIntercept impl.
    virtual void addWork(CThorExpandingRowArray *lgroup,CThorExpandingRowArray *rgroup)
    {
        noteGroupSizes(lgroup, rgroup);
        cWorkItem *item = new cWorkItem(activity, lgroup, rgroup);
        workqueue.enqueue(item);
    }
    virtual void addRow(const void *row)
    {
        assertex(row);
        rowWriter->putRow(row);
    }
};


IJoinHelper *createJoinHelper(CActivityBase &activity, IHThorJoinArg *helper, IThorRowInterfaces *rowIf, bool parallelmatch, bool unsortedoutput)
{
    // 
#ifdef TEST_PARALLEL_MATCH
    parallelmatch = true;
#endif
#ifdef TEST_UNSORTED_OUT
    unsortedoutput = true;
#endif
    IJoinHelper *jhelper = new CJoinHelper(activity, helper, rowIf);
    if (!parallelmatch||helper->getKeepLimit()||((helper->getJoinFlags()&JFslidingmatch)!=0)) // currently don't support betweenjoin or keep and multicore
        return jhelper;
    unsigned numthreads = activity.getOptInt(THOROPT_JOINHELPER_THREADS, 0);
    if (0 == numthreads)
        numthreads = activity.queryMaxCores();
    ActPrintLog(&activity, thorDetailedLogLevel, "Join helper using %d threads", numthreads);
    if (unsortedoutput)
        return new CMultiCoreUnorderedJoinHelper(activity, numthreads, false, jhelper, helper, rowIf);
    return new CMultiCoreJoinHelper(activity, numthreads, false, jhelper, helper, rowIf);
}


IJoinHelper *createSelfJoinHelper(CActivityBase &activity, IHThorJoinArg *helper, IThorRowInterfaces *rowIf, bool parallelmatch, bool unsortedoutput)
{
#ifdef TEST_PARALLEL_MATCH
    parallelmatch = true;
#endif
#ifdef TEST_UNSORTED_OUT
    unsortedoutput = true;
#endif
    IJoinHelper *jhelper = new SelfJoinHelper(activity, helper, rowIf);
    if (!parallelmatch||helper->getKeepLimit()||((helper->getJoinFlags()&JFslidingmatch)!=0)) // currently don't support betweenjoin or keep and multicore
        return jhelper;
    unsigned numthreads = activity.getOptInt(THOROPT_JOINHELPER_THREADS, 0);
    if (0 == numthreads)
        numthreads = activity.queryMaxCores();
    ActPrintLog(&activity, thorDetailedLogLevel, "Self join helper using %d threads", numthreads);
    if (unsortedoutput)
        return new CMultiCoreUnorderedJoinHelper(activity, numthreads, true, jhelper, helper, rowIf);
    return new CMultiCoreJoinHelper(activity, numthreads, true, jhelper, helper, rowIf);
}
