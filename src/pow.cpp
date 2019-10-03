// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

/* from zawy repo
 Preliminary code for super-fast increases in difficulty.
 Requires the ability to change the difficulty during the current block,
 based on the timestamp the miner selects. See my github issue #36 and KMD.
 Needs intr-block exponential decay function because
 this can make difficulty jump very high.
 Miners need to caclulate new difficulty with each second, or
 maybe 3 seconds.  FTL, MTP, and revert to local times must be small.
 MTP=1 if using Digishield. Out-of-sequence timestamps must be forbidden.
 1) bnTarget = Digishield() or other baseline DA
 2) bnTarget = RT_CST_RST()
 3) bnTarget = max(bnTarget,expdecay())
 RT_CST_RST() multiplies Recent Target(s), Current Solvetimes, &
 Recent SolveTime if RST had an unlikely 1/200 block chance of
 being too fast on accident. This estimates and adjusts for recent
 hashrate aggressively (lots of random error) but corrects the error by
 CST adjusting the difficulty during the block.
 It checks to see if there was an "active trigger" still in play which
 occurs when recent block emission rate has been too fast. Triggers
 are supposed to be active if emission rate has not slowed up enough
 to get back on track. It checks the longest range first because it's
 the least aggressive.
 T = target blocktime
 ts = timestamp vector, 62 elements, 62 is oldest  (elements needed are 50+W)
 ct = cumulative targets, 62 elements, 62 is oldest
 W = window size of recent solvetimes and targets to use that estimates hashrate
 numerator & deonominator needed for 1/200 possion estimator
 past = how far back in past to look for beginning of a trigger
 */

/* create ts and cw vectors
// Get bnTarget = Digishield();
arith_uint256 past = 50;
arith_uint256 W = 12;
arith_uint256 numerator = 12;
arith_uint256 denominator = 7;
// bnTarget = RT_CST_RST (bnTarget, ts, cw, numerator, denominator, W, T, past);
W = 6; top = 7; denominator = 3;
// bnTarget = RT_CST_RST (bnTarget, ts, cw, numerator, denominator, W, T, past);
W = 3; top = 1; denominator = 2;
bnTarget = RT_CST_RST (bnTarget, ts, cw, numerator, denominator, W, T, past);
*/

#define ASSETCHAINS_BLOCKTIME 10
#define T 10 // ASSETCHAIN_BLOCKTIME
#define K ((int64_t)1000000)

#ifdef original_algo
arith_uint256 oldRT_CST_RST(int32_t height,uint32_t nTime,arith_uint256 bnTarget,uint32_t *ts,arith_uint256 *ct,int32_t numerator,int32_t denominator,int32_t W,int32_t past)
{
    //if (ts.size() < 2*W || ct.size() < 2*W ) { exit; } // error. a vector was too small
    //if (ts.size() < past+W || ct.size() < past+W ) { past = min(ct.size(), ts.size()) - W; } // past was too small, adjust
    int64_t altK; int32_t i,j,k,ii=0; // K is a scaling factor for integer divisions
    if ( height < 64 )
        return(bnTarget);
    //if ( ((ts[0]-ts[W]) * W * 100)/(W-1) < (T * numerator * 100)/denominator )
    if ( (ts[0] - ts[W]) < (T * numerator)/denominator )
    {
        //bnTarget = ((ct[0]-ct[1])/K) * max(K,(K*(nTime-ts[0])*(ts[0]-ts[W])*denominator/numerator)/T/T);
        bnTarget = ct[0] / arith_uint256(K);
        //altK = (K * (nTime-ts[0]) * (ts[0]-ts[W]) * denominator * W) / (numerator * (W-1) * (T * T));
        altK = (K * (nTime-ts[0]) * (ts[0]-ts[W]) * denominator) / (numerator * (T * T));
        fprintf(stderr,"ht.%d initial altK.%lld %d * %d * %d / %d\n",height,(long long)altK,(nTime-ts[0]),(ts[0]-ts[W]),denominator,numerator);
        if ( altK > K )
            altK = K;
        bnTarget *= arith_uint256(altK);
        if ( altK < K )
            return(bnTarget);
    }
    /*  Check past 24 blocks for any sum of 3 STs < T/2 triggers. This is messy
     because the blockchain does not allow us to store a variable to know
     if we are currently in a triggered state that is making a sequence of
     adjustments to prevTargets, so we have to look for them.
     Nested loops do this: if block emission has not slowed to be back on track at
     any time since most recent trigger and we are at current block, aggressively
     adust prevTarget. */

    for (j=past-1; j>=2; j--)
    {
        if ( ts[j]-ts[j+W] < T*numerator/denominator )
        {
            ii = 0;
            for (i=j-2; i>=0; i--)
            {
                ii++;
                // Check if emission caught up. If yes, "trigger stopped at i".
                // Break loop to try more recent j's to see if trigger activates again.
                if ( (ts[i] - ts[j+W]) > (ii+W)*T )
                    break;

                // We're here, so there was a TS[j]-TS[j-3] < T/2 trigger in the past and emission rate has not yet slowed up to be back on track so the "trigger is still active", aggressively adjusting target here at block "i"
                if ( i == 0 )
                {
                    /* We made it all the way to current block. Emission rate since
                     last trigger never slowed enough to get back on track, so adjust again.
                     If avg last 3 STs = T, this increases target to prevTarget as ST increases to T.
                     This biases it towards ST=~1.75*T to get emission back on track.
                     If avg last 3 STs = T/2, target increases to prevTarget at 2*T.
                     Rarely, last 3 STs can be 1/2 speed => target = prevTarget at T/2, & 1/2 at T.*/

                    //bnTarget = ((ct[0]-ct[W])/W/K) * (K*(nTime-ts[0])*(ts[0]-ts[W]))/W/T/T;
                    bnTarget = ct[0];
                    for (k=1; k<W; k++)
                        bnTarget += ct[k];
                    bnTarget /= arith_uint256(W * K);
                    altK = (K * (nTime-ts[0]) * (ts[0]-ts[W])) / (W * T * T);
                    fprintf(stderr,"ht.%d made it to i == 0, j.%d ii.%d altK %lld (%d * %d) %u - %u W.%d\n",height,j,ii,(long long)altK,(nTime-ts[0]),(ts[0]-ts[W]),ts[0],ts[W],W);
                    bnTarget *= arith_uint256(altK);
                    j = 0; // It needed adjusting, we adjusted it, we're finished, so break out of j loop.
                }
            }
        }
    }
    return(bnTarget);
}
#endif

arith_uint256 RT_CST_RST_outer(int32_t height,uint32_t nTime,arith_uint256 bnTarget,uint32_t *ts,arith_uint256 *ct,int32_t numerator,int32_t denominator,int32_t W,int32_t past)
{
    int64_t outerK; arith_uint256 mintarget = bnTarget / arith_uint256(2);
    if ( (ts[0] - ts[W]) < (T * numerator)/denominator )
    {
        outerK = (K * (nTime-ts[0]) * (ts[0]-ts[W]) * denominator) / (numerator * (T * T));
        if ( outerK < K )
        {
            bnTarget = ct[0] / arith_uint256(K);
            bnTarget *= arith_uint256(outerK);
        }
        if ( bnTarget > mintarget )
            bnTarget = mintarget;
        {
            int32_t z;
            for (z=31; z>=0; z--)
                fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[z]);
        }
        fprintf(stderr," ht.%d initial outerK.%lld %d * %d * %d / %d\n",height,(long long)outerK,(nTime-ts[0]),(ts[0]-ts[W]),denominator,numerator);
    } //else fprintf(stderr,"ht.%d no outer trigger %d >= %d\n",height,(ts[0] - ts[W]),(T * numerator)/denominator);
    return(bnTarget);
}

arith_uint256 RT_CST_RST_target(int32_t height,uint32_t nTime,arith_uint256 bnTarget,uint32_t *ts,arith_uint256 *ct,int32_t width)
{
    int32_t i; int64_t innerK;
    bnTarget = ct[0];
    for (i=1; i<width; i++)
        bnTarget += ct[i];
    bnTarget /= arith_uint256(width * K);
    innerK = (K * (nTime-ts[0]) * (ts[0]-ts[width])) / (width * T * T);
    bnTarget *= arith_uint256(innerK);
    if ( 0 )
    {
        int32_t z;
        for (z=31; z>=0; z--)
            fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[z]);
        fprintf(stderr," ht.%d innerK %lld (%d * %d) %u - %u width.%d\n",height,(long long)innerK,(nTime-ts[0]),(ts[0]-ts[width]),ts[0],ts[width],width);
    }
    return(bnTarget);
}

arith_uint256 RT_CST_RST_inner(int32_t height,uint32_t nTime,arith_uint256 bnTarget,uint32_t *ts,arith_uint256 *ct,int32_t W,int32_t outeri)
{
    arith_uint256 mintarget; int32_t expected,elapsed,width = outeri+W;
    expected = (width+1) * T;
    if ( (elapsed= (ts[0] - ts[width])) < expected )
    {
        mintarget = (bnTarget / arith_uint256(11)) * arith_uint256(10);
        bnTarget = RT_CST_RST_target(height,nTime,bnTarget,ts,ct,W);
        if ( bnTarget > mintarget ) // force zawyflag to 1
            bnTarget = mintarget;
        {
            int32_t z;
            for (z=31; z>=0; z--)
                fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[z]);
        }
        fprintf(stderr," height.%d O.%-2d, W.%-2d width.%-2d %4d vs %-4d, deficit %4d tip.%d\n",height,outeri,W,width,(ts[0] - ts[width]),expected,expected - (ts[0] - ts[width]),nTime-ts[0]);
    }
    return(bnTarget);
}

arith_uint256 zawy_targetMA(arith_uint256 easy,arith_uint256 bnSum,int32_t num,int32_t numerator,int32_t divisor)
{
    bnSum /= arith_uint256(10 * num * num * divisor); // ASSETCHAINS_BLOCKTIME
    bnSum *= arith_uint256(numerator);
    if ( bnSum > easy )
        bnSum = easy;
    return(bnSum);
}

int64_t zawy_exponential_val360000(int32_t num)
{
    int32_t i,n,modval; int64_t A = 1, B = 3600 * 100;
    if ( (n= (num/ASSETCHAINS_BLOCKTIME)) > 0 )
    {
        for (i=1; i<=n; i++)
            A *= 3;
    }
    if ( (modval= (num % ASSETCHAINS_BLOCKTIME)) != 0 )
    {
        B += (3600 * 110 * modval) / ASSETCHAINS_BLOCKTIME;
        B += (3600 * 60 * modval * modval) / (ASSETCHAINS_BLOCKTIME * ASSETCHAINS_BLOCKTIME);
    }
    return(A * B);
}

arith_uint256 zawy_exponential(arith_uint256 bnTarget,int32_t mult)
{
    bnTarget /= arith_uint256(100 * 3600);
    bnTarget *= arith_uint256(zawy_exponential_val360000(mult));
    return(bnTarget);
}

arith_uint256 zawy_ctB(arith_uint256 bnTarget,uint32_t solvetime)
{
    int64_t num;
    num = ((int64_t)1000 * solvetime * solvetime * 1000) / (T * T * 784);
    if ( num > 1 )
    {
        bnTarget /= arith_uint256(1000);
        bnTarget *= arith_uint256(num);
    }
    return(bnTarget);
}

arith_uint256 zawy_TSA_EMA(int32_t height,int32_t tipdiff,arith_uint256 prevTarget,int32_t solvetime)
{
    arith_uint256 A,B,C,bnTarget;
    if ( tipdiff < 4 )
        tipdiff = 4;
    tipdiff &= ~1;
    bnTarget = prevTarget / arith_uint256(K*T);
    A = bnTarget * arith_uint256(T);
    B = (bnTarget / arith_uint256(360000)) * arith_uint256(tipdiff * zawy_exponential_val360000(tipdiff/2));
    C = (bnTarget / arith_uint256(360000)) * arith_uint256(T * zawy_exponential_val360000(tipdiff/2));
    bnTarget = ((A + B - C) / arith_uint256(tipdiff)) * arith_uint256(K*T);
    {
        int32_t z;
        for (z=31; z>=0; z--)
            fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[z]);
    }
    fprintf(stderr," ht.%d TSA bnTarget tipdiff.%d\n",height,tipdiff);
    return(bnTarget);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
//    if (ASSETCHAINS_ALGO != ASSETCHAINS_EQUIHASH && ASSETCHAINS_STAKED == 0)
        return lwmaGetNextWorkRequired(pindexLast, pblock, params);
/*
    arith_uint256 bnLimit;
    if (ASSETCHAINS_ALGO == ASSETCHAINS_EQUIHASH)
        bnLimit = UintToArith256(params.powLimit);
    else
        bnLimit = UintToArith256(params.powAlternate);
    unsigned int nProofOfWorkLimit = bnLimit.GetCompact();
    // Genesis block
    if (pindexLast == NULL )
        return nProofOfWorkLimit;

    //{
        // Comparing to pindexLast->nHeight with >= because this function
        // returns the work required for the block after pindexLast.
        //if (params.nPowAllowMinDifficultyBlocksAfterHeight != boost::none &&
        //    pindexLast->nHeight >= params.nPowAllowMinDifficultyBlocksAfterHeight.get())
        //{
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 6 * 2.5 minutes
            // then allow mining of a min-difficulty block.
        //    if (pblock && pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 6)
        //        return nProofOfWorkLimit;
        //}
    //}

    // Find the first block in the averaging interval
    const CBlockIndex* pindexFirst = pindexLast;
    arith_uint256 ct[64],ctinv[64],bnTmp,bnPrev,bnTarget,bnTarget6,bnTarget12,bnTot {0};
    uint32_t nbits,blocktime,ts[sizeof(ct)/sizeof(*ct)]; int32_t zflags[sizeof(ct)/sizeof(*ct)],i,diff,height=0,mult = 0,tipdiff = 0;
    memset(ts,0,sizeof(ts));
    memset(ct,0,sizeof(ct));
    memset(ctinv,0,sizeof(ctinv));
    memset(zflags,0,sizeof(zflags));
    if ( pindexLast != 0 )
        height = (int32_t)pindexLast->GetHeight() + 1;
    if ( ASSETCHAINS_ADAPTIVEPOW > 0 && pindexFirst != 0 && pblock != 0 && height >= (int32_t)(sizeof(ct)/sizeof(*ct)) )
    {
        tipdiff = (pblock->nTime - pindexFirst->nTime);
        mult = tipdiff - 7 * ASSETCHAINS_BLOCKTIME;
        bnPrev.SetCompact(pindexFirst->nBits);
        for (i=0; pindexFirst != 0 && i<(int32_t)(sizeof(ct)/sizeof(*ct)); i++)
        {
            zflags[i] = (pindexFirst->nBits & 3);
            ct[i].SetCompact(pindexFirst->nBits);
            ts[i] = pindexFirst->nTime;
            pindexFirst = pindexFirst->pprev;
        }
        for (i=0; pindexFirst != 0 && i<(int32_t)(sizeof(ct)/sizeof(*ct))-1; i++)
        {
            if ( zflags[i] == 1 || zflags[i] == 2 ) // I, O and if TSA made it harder
                ct[i] = zawy_ctB(ct[i],ts[i] - ts[i+1]);
        }
        if ( ASSETCHAINS_ADAPTIVEPOW == 2 ) // TSA
        {
            bnTarget = zawy_TSA_EMA(height,tipdiff,ct[0],ts[0] - ts[1]);
            nbits = bnTarget.GetCompact();
            nbits = (nbits & 0xfffffffc) | 0;
            return(nbits);
        }
    }
    pindexFirst = pindexLast;
    for (i = 0; pindexFirst && i < params.nPowAveragingWindow; i++)
    {
        bnTmp.SetCompact(pindexFirst->nBits);
        if ( ASSETCHAINS_ADAPTIVEPOW > 0 && pblock != 0 )
        {
            blocktime = pindexFirst->nTime;
            diff = (pblock->nTime - blocktime);
            //fprintf(stderr,"%d ",diff);
            if ( i < 6 )
            {
                diff -= (8+i)*ASSETCHAINS_BLOCKTIME;
                if ( diff > mult )
                {
                    //fprintf(stderr,"i.%d diff.%d (%u - %u - %dx)\n",i,(int32_t)diff,pblock->nTime,pindexFirst->nTime,(8+i));
                    mult = diff;
                }
            }
            //if ( zflags[i] != 0 && zflags[0] != 0 )
            //    bnTmp = (ct[i] / arith_uint256(3));
        }
        bnTot += bnTmp;
        pindexFirst = pindexFirst->pprev;
    }
    //fprintf(stderr,"diffs %d\n",height);
    // Check we have enough blocks
    if (pindexFirst == NULL)
        return nProofOfWorkLimit;

    bool fNegative,fOverflow; int32_t past,zawyflag = 0; arith_uint256 easy,origtarget,bnAvg {bnTot / params.nPowAveragingWindow};
    nbits = CalculateNextWorkRequired(bnAvg, pindexLast->GetMedianTimePast(), pindexFirst->GetMedianTimePast(), params);
    if ( ASSETCHAINS_ADAPTIVEPOW > 0 )
    {
        bnTarget = arith_uint256().SetCompact(nbits);
        if ( height > (int32_t)(sizeof(ct)/sizeof(*ct)) && pblock != 0 && tipdiff > 0 )
        {
            easy.SetCompact(KOMODO_MINDIFF_NBITS & (~3),&fNegative,&fOverflow);
            if ( pblock != 0 )
            {
                origtarget = bnTarget;
                past = 20;
                if ( zflags[0] == 0 || zflags[0] == 3 )
                {
                    bnTarget = RT_CST_RST_outer(height,pblock->nTime,bnTarget,ts,ct,1,2,3,past);
                    if ( bnTarget < origtarget )
                        zawyflag = 2;
                    else
                    {
                        bnTarget = RT_CST_RST_outer(height,pblock->nTime,bnTarget,ts,ct,7,3,6,past+10);
                        if ( bnTarget < origtarget )
                            zawyflag = 2;
                        else
                        {
                            bnTarget = RT_CST_RST_outer(height,pblock->nTime,bnTarget,ts,ct,12,7,12,past+20);
                            if ( bnTarget < origtarget )
                                zawyflag = 2;
                        }
                    }
                }
                else
                {
                    for (i=0; i<40; i++)
                        if ( zflags[i] == 2 )
                            break;
                    if ( i < 40 )
                    {
                        bnTarget = RT_CST_RST_inner(height,pblock->nTime,bnTarget,ts,ct,3,i);
                        bnTarget6 = RT_CST_RST_inner(height,pblock->nTime,bnTarget,ts,ct,6,i);
                        bnTarget12 = RT_CST_RST_inner(height,pblock->nTime,bnTarget,ts,ct,12,i);
                        if ( bnTarget6 < bnTarget12 )
                            bnTmp = bnTarget6;
                        else bnTmp = bnTarget12;
                        if ( bnTmp < bnTarget )
                            bnTarget = bnTmp;
                        if ( bnTarget != origtarget )
                            zawyflag = 1;
                    }
                }
            }
            if ( mult > 1 ) // e^mult case, jl777:  test of mult > 1 failed when it was int64_t???
            {
                origtarget = bnTarget;
                bnTarget = zawy_exponential(bnTarget,mult);
                if ( bnTarget < origtarget || bnTarget > easy )
                {
                    bnTarget = easy;
                    fprintf(stderr,"cmp.%d mult.%d ht.%d -> easy target\n",mult>1,(int32_t)mult,height);
                    return(KOMODO_MINDIFF_NBITS & (~3));
                }
                {
                    int32_t z;
                    for (z=31; z>=0; z--)
                        fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[z]);
                }
                fprintf(stderr," exp() to the rescue cmp.%d mult.%d for ht.%d\n",mult>1,(int32_t)mult,height);
            }
            if ( 0 && zflags[0] == 0 && zawyflag == 0 && mult <= 1 )
            {
                bnTarget = zawy_TSA_EMA(height,tipdiff,(bnTarget+ct[0]+ct[1])/arith_uint256(3),ts[0] - ts[1]);
                if ( bnTarget < origtarget )
                    zawyflag = 3;
            }
        }
        nbits = bnTarget.GetCompact();
        nbits = (nbits & 0xfffffffc) | zawyflag;
    }
    return(nbits);
    */
}

unsigned int CalculateNextWorkRequired(arith_uint256 bnAvg,
                                       int64_t nLastBlockTime, int64_t nFirstBlockTime,
                                       const Consensus::Params& params)
{
    // Limit adjustment step
    // Use medians to prevent time-warp attacks
    int64_t nActualTimespan = nLastBlockTime - nFirstBlockTime;
    printf("pow   nActualTimespan = %d  before dampening\n", nActualTimespan);
    nActualTimespan = params.AveragingWindowTimespan() + (nActualTimespan - params.AveragingWindowTimespan())/4;
    printf("pow   nActualTimespan = %d  before bounds\n", nActualTimespan);

    if ( 1 <= 0 )
    {
        if (nActualTimespan < params.MinActualTimespan())
            nActualTimespan = params.MinActualTimespan();
        if (nActualTimespan > params.MaxActualTimespan())
            nActualTimespan = params.MaxActualTimespan();
    }
    // Retarget
    arith_uint256 bnLimit;
    if (1) //ASSETCHAINS_ALGO == ASSETCHAINS_EQUIHASH)
        bnLimit = UintToArith256(params.powLimit);
//    else
//        bnLimit = UintToArith256(params.powAlternate);

    const arith_uint256 bnPowLimit = bnLimit; //UintToArith256(params.powLimit);
    arith_uint256 bnNew {bnAvg};
    bnNew /= params.AveragingWindowTimespan();
    bnNew *= nActualTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    /// debug print
    printf("pow GetNextWorkRequired RETARGET\n");
    printf("pow params.AveragingWindowTimespan() = %d    nActualTimespan = %d\n", params.AveragingWindowTimespan(), nActualTimespan);
    printf("pow Current average: %08x  %s\n", bnAvg.GetCompact(), bnAvg.ToString());
    printf("pow After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());

    return bnNew.GetCompact();
}

unsigned int lwmaGetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    return lwmaCalculateNextWorkRequired(pindexLast, params);
}

unsigned int lwmaCalculateNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    arith_uint256 nextTarget {0}, sumTarget {0}, bnTmp, bnLimit;
    //if (ASSETCHAINS_ALGO == ASSETCHAINS_EQUIHASH)
        bnLimit = UintToArith256(params.powLimit);
    //else
    //    bnLimit = UintToArith256(params.powAlternate);

    unsigned int nProofOfWorkLimit = bnLimit.GetCompact();
    
    //printf("PoWLimit: %u\n", nProofOfWorkLimit);

    // Find the first block in the averaging interval as we total the linearly weighted average
    const CBlockIndex* pindexFirst = pindexLast;
    const CBlockIndex* pindexNext;
    int64_t t = 0, solvetime, k = params.nLwmaAjustedWeight, N = params.nPowAveragingWindow;

    for (int i = 0, j = N - 1; pindexFirst && i < N; i++, j--) {
        pindexNext = pindexFirst;
        pindexFirst = pindexFirst->pprev;
        if (!pindexFirst)
            break;

        solvetime = pindexNext->GetBlockTime() - pindexFirst->GetBlockTime();

        // weighted sum
        t += solvetime * j;

        // Target sum divided by a factor, (k N^2).
        // The factor is a part of the final equation. However we divide 
        // here to avoid potential overflow.
        bnTmp.SetCompact(pindexNext->nBits);
        sumTarget += bnTmp / (k * N * N);
    }

    // Check we have enough blocks
    if (!pindexFirst)
        return nProofOfWorkLimit;

    // Keep t reasonable in case strange solvetimes occurred.
    if (t < N * k / 3)
        t = N * k / 3;

    bnTmp = bnLimit;
    nextTarget = t * sumTarget;
    if (nextTarget > bnTmp)
        nextTarget = bnTmp;

    return nextTarget.GetCompact();
}

bool DoesHashQualify(const CBlockIndex *pbindex)
{
    // if it fails hash test and PoW validation, consider it POS. it could also be invalid
    arith_uint256 hash = UintToArith256(pbindex->GetBlockHash());
    // to be considered POS, we first can't qualify as POW
    if (hash > hash.SetCompact(pbindex->nBits))
    {
        return false;
    }
    return true;
}

// the goal is to keep POS at a solve time that is a ratio of block time units. the low resolution makes a stable solution more challenging
// and requires that the averaging window be quite long.
uint32_t lwmaGetNextPOSRequired(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    arith_uint256 nextTarget {0}, sumTarget {0}, bnTmp, bnLimit;
 /*
    bnLimit = UintToArith256(params.posLimit);
    uint32_t nProofOfStakeLimit = bnLimit.GetCompact();
    int64_t t = 0, solvetime = 0;
    int64_t k = params.nLwmaPOSAjustedWeight;
    int64_t N = params.nPOSAveragingWindow;

    struct solveSequence {
        int64_t solveTime;
        bool consecutive;
        uint32_t nBits;
        solveSequence()
        {
            consecutive = 0;
            solveTime = 0;
            nBits = 0;
        }
    };

    // Find the first block in the averaging interval as we total the linearly weighted average
    // of POS solve times
    const CBlockIndex* pindexFirst = pindexLast;

    // we need to make sure we have a starting nBits reference, which is either the last POS block, or the default
    // if we have had no POS block in the threshold number of blocks, we must return the default, otherwise, we'll now have
    // a starting point
    uint32_t nBits = nProofOfStakeLimit;
    for (int64_t i = 0; i < VERUS_NOPOS_THRESHHOLD; i++)
    {
        if (!pindexFirst)
            return nProofOfStakeLimit;

        CBlockHeader hdr = pindexFirst->GetBlockHeader();

        if (hdr.IsVerusPOSBlock())
        {
            nBits = hdr.GetVerusPOSTarget();
            break;
        }
        pindexFirst = pindexFirst->pprev;
    }

    pindexFirst = pindexLast;
    std::vector<solveSequence> idx = std::vector<solveSequence>();
    idx.resize(N);

    for (int64_t i = N - 1; i >= 0; i--)
    {
        // we measure our solve time in passing of blocks, where one bock == VERUS_BLOCK_POSUNITS units
        // consecutive blocks in either direction have their solve times exponentially multiplied or divided by power of 2
        int x;
        for (x = 0; x < VERUS_CONSECUTIVE_POS_THRESHOLD; x++)
        {
            pindexFirst = pindexFirst->pprev;

            if (!pindexFirst)
                return nProofOfStakeLimit;

            CBlockHeader hdr = pindexFirst->GetBlockHeader();
            if (hdr.IsVerusPOSBlock())
            {
                nBits = hdr.GetVerusPOSTarget();
                break;
            }
        }

        if (x)
        {
            idx[i].consecutive = false;
            if (!memcmp(ASSETCHAINS_SYMBOL, "VRSC", 4) && pindexLast->GetHeight() < 67680)
            {
                idx[i].solveTime = VERUS_BLOCK_POSUNITS * (x + 1);
            }
            else
            {
                int64_t lastSolveTime = 0;
                idx[i].solveTime = VERUS_BLOCK_POSUNITS;
                for (int64_t j = 0; j < x; j++)
                {
                    lastSolveTime = VERUS_BLOCK_POSUNITS + (lastSolveTime >> 1);
                    idx[i].solveTime += lastSolveTime;
                }
            }
            idx[i].nBits = nBits;
        }
        else
        {
            idx[i].consecutive = true;
            idx[i].nBits = nBits;
            // go forward and halve the minimum solve time for all consecutive blocks in this run, to get here, our last block is POS,
            // and if there is no POS block in front of it, it gets the normal solve time of one block
            uint32_t st = VERUS_BLOCK_POSUNITS;
            for (int64_t j = i; j < N; j++)
            {
                if (idx[j].consecutive == true)
                {
                    idx[j].solveTime = st;
                    if ((j - i) >= VERUS_CONSECUTIVE_POS_THRESHOLD)
                    {
                        // if this is real time, return zero
                        if (j == (N - 1))
                        {
                            // target of 0 (virtually impossible), if we hit max consecutive POS blocks
                            nextTarget.SetCompact(0);
                            return nextTarget.GetCompact();
                        }
                    }
                    st >>= 1;
                }
                else
                    break;
            }
        }
    }

    for (int64_t i = N - 1; i >= 0; i--) 
    {
        // weighted sum
        t += idx[i].solveTime * i;

        // Target sum divided by a factor, (k N^2).
        // The factor is a part of the final equation. However we divide 
        // here to avoid potential overflow.
        bnTmp.SetCompact(idx[i].nBits);
        sumTarget += bnTmp / (k * N * N);
    }

    // Keep t reasonable in case strange solvetimes occurred.
    if (t < N * k / 3)
        t = N * k / 3;

    nextTarget = t * sumTarget;
    if (nextTarget > bnLimit)
        nextTarget = bnLimit;
*/
    return nextTarget.GetCompact();
}


unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (pindexLast->nHeight < params.nAdaptativePoWActivationThreshold) {
        if (params.fPowNoRetargeting)
            return pindexLast->nBits;

        // Limit adjustment step
        int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
        if (nActualTimespan < params.nPowTargetTimespan/4)
            nActualTimespan = params.nPowTargetTimespan/4;
        if (nActualTimespan > params.nPowTargetTimespan*4)
            nActualTimespan = params.nPowTargetTimespan*4;

        // Retarget
        const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
        arith_uint256 bnNew;
        bnNew.SetCompact(pindexLast->nBits);
        bnNew *= nActualTimespan;
        bnNew /= params.nPowTargetTimespan;
        if ( 0 )
        {
            int32_t i;
            for (i=31; i>=0; i--)
                printf("%02x",((uint8_t *)&bnNew)[i]);
            printf("bnNew vs limit ");
            for (i=31; i>=0; i--)
                printf("%02x",((uint8_t *)&bnPowLimit)[i]);
            printf("\n");
        }
        if (bnNew > bnPowLimit)
            bnNew = bnPowLimit;

        return bnNew.GetCompact();
        }
    else    {
            // Limit adjustment step
    // Use medians to prevent time-warp attacks
    int64_t nActualTimespan = nLastBlockTime - nFirstBlockTime;
    LogPrint("pow", "  nActualTimespan = %d  before dampening\n", nActualTimespan);
    nActualTimespan = params.AveragingWindowTimespan() + (nActualTimespan - params.AveragingWindowTimespan())/4;
    LogPrint("pow", "  nActualTimespan = %d  before bounds\n", nActualTimespan);

    if ( ASSETCHAINS_ADAPTIVEPOW <= 0 )
    {
        if (nActualTimespan < params.MinActualTimespan())
            nActualTimespan = params.MinActualTimespan();
        if (nActualTimespan > params.MaxActualTimespan())
            nActualTimespan = params.MaxActualTimespan();
    }
    // Retarget
    arith_uint256 bnLimit;
    if (ASSETCHAINS_ALGO == ASSETCHAINS_EQUIHASH)
        bnLimit = UintToArith256(params.powLimit);
    else
        bnLimit = UintToArith256(params.powAlternate);

    const arith_uint256 bnPowLimit = bnLimit; //UintToArith256(params.powLimit);
    arith_uint256 bnNew {bnAvg};
    bnNew /= params.nPowTargetTimespan;
    bnNew *= nActualTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    /// debug print
    LogPrint("pow", "GetNextWorkRequired RETARGET\n");
    LogPrint("pow", "params.nPowTargetTimespan = %d    nActualTimespan = %d\n", params.nPowTargetTimespan, nActualTimespan);
    LogPrint("pow", "Current average: %08x  %s\n", bnAvg.GetCompact(), bnAvg.ToString());
    LogPrint("pow", "After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());

    return bnNew.GetCompact();
}

unsigned int lwmaGetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    return lwmaCalculateNextWorkRequired(pindexLast, params);
}

unsigned int lwmaCalculateNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    arith_uint256 nextTarget {0}, sumTarget {0}, bnTmp, bnLimit;
    if (ASSETCHAINS_ALGO == ASSETCHAINS_EQUIHASH)
        bnLimit = UintToArith256(params.powLimit);
    else
        bnLimit = UintToArith256(params.powAlternate);

    unsigned int nProofOfWorkLimit = bnLimit.GetCompact();
    
    //printf("PoWLimit: %u\n", nProofOfWorkLimit);

    // Find the first block in the averaging interval as we total the linearly weighted average
    const CBlockIndex* pindexFirst = pindexLast;
    const CBlockIndex* pindexNext;
    int64_t t = 0, solvetime, k = params.nLwmaAjustedWeight, N = params.nPowAveragingWindow;

    for (int i = 0, j = N - 1; pindexFirst && i < N; i++, j--) {
        pindexNext = pindexFirst;
        pindexFirst = pindexFirst->pprev;
        if (!pindexFirst)
            break;

        solvetime = pindexNext->GetBlockTime() - pindexFirst->GetBlockTime();

        // weighted sum
        t += solvetime * j;

        // Target sum divided by a factor, (k N^2).
        // The factor is a part of the final equation. However we divide 
        // here to avoid potential overflow.
        bnTmp.SetCompact(pindexNext->nBits);
        sumTarget += bnTmp / (k * N * N);
    }

    // Check we have enough blocks
    if (!pindexFirst)
        return nProofOfWorkLimit;

    // Keep t reasonable in case strange solvetimes occurred.
    if (t < N * k / 3)
        t = N * k / 3;

    bnTmp = bnLimit;
    nextTarget = t * sumTarget;
    if (nextTarget > bnTmp)
        nextTarget = bnTmp;

    return nextTarget.GetCompact();
        }
}

bool DoesHashQualify(const CBlockIndex *pbindex)
{
    // if it fails hash test and PoW validation, consider it POS. it could also be invalid
    arith_uint256 hash = UintToArith256(pbindex->GetBlockHash());
    // to be considered POS, we first can't qualify as POW
    if (hash > hash.SetCompact(pbindex->nBits))
    {
        return false;
    }
    return true;
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
    {
        arith_uint256 tmp; int32_t i;
        tmp = UintToArith256(params.powLimit);
        for (i=31; i>=0; i--)
            printf("%02x",((uint8_t *)&bnTarget)[i]);
        printf(" bntarget vs powlimit ");
        for (i=31; i>=0; i--)
            printf("%02x",((uint8_t *)&tmp)[i]);
        printf("overflow or bad target\n");
        return false;
    }
    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
    {
        /*arith_uint256 tmp; int32_t i;
        tmp = UintToArith256(hash);
        for (i=31; i>=0; i--)
            printf("%02x",((uint8_t *)&tmp)[i]);
        printf(" hash vs target ");
        for (i=31; i>=0; i--)
            printf("%02x",((uint8_t *)&bnTarget)[i]);
        printf("\n");*/
        return false;
    }
    return true;
}
