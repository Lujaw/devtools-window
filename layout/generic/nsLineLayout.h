/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 *   Steve Clark <buster@netscape.com>
 *   Robert O'Callahan <roc+moz@cs.cmu.edu>
 */
#ifndef nsLineLayout_h___
#define nsLineLayout_h___

#include "nsFrame.h"
#include "nsVoidArray.h"
#include "nsTextRun.h"
#include "nsLineBox.h"

class nsISpaceManager;
class nsBlockReflowState;
class nsPlaceholderFrame;
struct nsStyleText;

#define NS_LINELAYOUT_NUM_FRAMES        5
#define NS_LINELAYOUT_NUM_SPANS         5

class nsLineLayout {
public:
  nsLineLayout(nsIPresContext* aPresContext,
               nsISpaceManager* aSpaceManager,
               const nsHTMLReflowState* aOuterReflowState,
               PRBool aComputeMaxElementSize);
  nsLineLayout(nsIPresContext* aPresContext);
  ~nsLineLayout();

  void Init(nsBlockReflowState* aState, nscoord aMinLineHeight,
            PRInt32 aLineNumber) {
    mBlockRS = aState;
    mMinLineHeight = aMinLineHeight;
    mLineNumber = aLineNumber;
  }

  PRInt32 GetColumn() {
    return mColumn;
  }

  void SetColumn(PRInt32 aNewColumn) {
    mColumn = aNewColumn;
  }
  
  PRInt32 GetLineNumber() const {
    return mLineNumber;
  }

  void BeginLineReflow(nscoord aX, nscoord aY,
                       nscoord aWidth, nscoord aHeight,
                       PRBool aImpactedByFloaters,
                       PRBool aIsTopOfPage);

  void EndLineReflow();

  void UpdateBand(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight,
                  PRBool aPlacedLeftFloater,
                  nsIFrame* aFloaterFrame);

  nsresult BeginSpan(nsIFrame* aFrame,
                     const nsHTMLReflowState* aSpanReflowState,
                     nscoord aLeftEdge,
                     nscoord aRightEdge);

  void EndSpan(nsIFrame* aFrame, nsSize& aSizeResult,
               nsSize* aMaxElementSize);

  PRBool InStrictMode();

  PRInt32 GetCurrentSpanCount() const;

  void SplitLineTo(PRInt32 aNewCount);

  PRBool IsZeroHeight();

  // Reflows the frame and returns the reflow status. aPushedFrame is PR_TRUE
  // if the frame is pushed to the next line because it doesn't fit
  nsresult ReflowFrame(nsIFrame* aFrame,
                       nsIFrame** aNextRCFrame,
                       nsReflowStatus& aReflowStatus,
                       nsHTMLReflowMetrics* aMetrics,
                       PRBool& aPushedFrame);

  nscoord GetCarriedOutBottomMargin() const {
    return mCarriedOutBottomMargin;
  }

  nsresult AddBulletFrame(nsIFrame* aFrame,
                          const nsHTMLReflowMetrics& aMetrics);

  void RemoveBulletFrame(nsIFrame* aFrame) {
    PushFrame(aFrame);
  }

  void VerticalAlignFrames(nsLineBox* aLineBox,
                           nsSize& aMaxElementSizeResult);

  PRBool TrimTrailingWhiteSpace();

  PRBool HorizontalAlignFrames(nsRect& aLineBounds,
                               PRBool aAllowJustify,
                               PRBool aShrinkWrapWidth);

  void RelativePositionFrames(nsRect& aCombinedArea);

  //----------------------------------------

  // Supporting methods and data for flags
protected:
#define LL_ENDSINWHITESPACE            0x00000001
#define LL_UNDERSTANDSNWHITESPACE      0x00000002
#define LL_TEXTSTARTSWITHNBSP          0x00000004
#define LL_FIRSTLETTERSTYLEOK          0x00000008
#define LL_ISTOPOFPAGE                 0x00000010
#define LL_UPDATEDBAND                 0x00000020
#define LL_IMPACTEDBYFLOATERS          0x00000040
#define LL_LASTFLOATERWASLETTERFRAME   0x00000080
#define LL_CANPLACEFLOATER             0x00000100
#define LL_KNOWSTRICTMODE              0x00000200
#define LL_INSTRICTMODE                0x00000400
#define LL_LINEENDSINBR                0x00000800
#define LL_LASTFLAG                    LL_LINEENDSINBR

  PRUint16 mFlags;

  void SetFlag(PRUint32 aFlag, PRBool aValue)
  {
    NS_ASSERTION(aFlag<=LL_LASTFLAG, "bad flag");
    NS_ASSERTION(aValue==PR_FALSE || aValue==PR_TRUE, "bad value");
    if (aValue) { // set flag
      mFlags |= aFlag;
    }
    else {        // unset flag
      mFlags &= ~aFlag;
    }
  }

  PRBool GetFlag(PRUint32 aFlag) const
  {
    NS_ASSERTION(aFlag<=LL_LASTFLAG, "bad flag");
    PRBool result = (mFlags & aFlag);
    if (result) return PR_TRUE;
    return PR_FALSE;
  }

public:

  // Support methods for white-space compression and word-wrapping
  // during line reflow

  void SetEndsInWhiteSpace(PRBool aState) {
    SetFlag(LL_ENDSINWHITESPACE, aState);
  }

  PRBool GetEndsInWhiteSpace() const {
    return GetFlag(LL_ENDSINWHITESPACE);
  }

  void SetUnderstandsWhiteSpace(PRBool aSetting) {
    SetFlag(LL_UNDERSTANDSNWHITESPACE, aSetting);
  }

  void SetTextJustificationWeights(PRInt32 aNumSpaces, PRInt32 aNumLetters) {
    mTextJustificationNumSpaces = aNumSpaces;
    mTextJustificationNumLetters = aNumLetters;
  }


  void SetTextStartsWithNBSP(PRBool aYes) {
    SetFlag(LL_TEXTSTARTSWITHNBSP, aYes);
  }

  void RecordWordFrame(nsIFrame* aWordFrame) {
    mWordFrames.AppendElement(aWordFrame);
  }

  PRBool InWord() const {
    return 0 != mWordFrames.Count();
  }

  void ForgetWordFrame(nsIFrame* aFrame);

  void ForgetWordFrames() {
    mWordFrames.Clear();
  }

  nsIFrame* FindNextText(nsIFrame* aFrame);

  PRBool CanPlaceFloaterNow() const;

  PRBool LineIsEmpty() const;

  PRBool LineIsBreakable() const;

  PRBool GetLineEndsInBR() const 
  { 
    return GetFlag(LL_LINEENDSINBR); 
  }

  void SetLineEndsInBR(PRBool aOn) 
  { 
    SetFlag(LL_LINEENDSINBR, aOn); 
  }

  //----------------------------------------
  // Inform the line-layout about the presence of a floating frame
  // XXX get rid of this: use get-frame-type?
  void InitFloater(nsPlaceholderFrame* aFrame);
  void AddFloater(nsPlaceholderFrame* aFrame);

  //----------------------------------------

  PRBool GetFirstLetterStyleOK() const {
    return GetFlag(LL_FIRSTLETTERSTYLEOK);
  }

  void SetFirstLetterStyleOK(PRBool aSetting) {
    SetFlag(LL_FIRSTLETTERSTYLEOK, aSetting);
  }

  void SetFirstLetterFrame(nsIFrame* aFrame) {
    mFirstLetterFrame = aFrame;
  }

  //----------------------------------------
  // Text run usage methods. These methods are using during reflow to
  // track the current text run and to advance through text runs.

  void SetReflowTextRuns(nsTextRun* aTextRuns) {
    mReflowTextRuns = aTextRuns;
  }

  //----------------------------------------

  static PRBool TreatFrameAsBlock(nsIFrame* aFrame);

  //----------------------------------------

  // XXX Move this out of line-layout; make some little interface to
  // deal with it...

  // Add another piece of text to a text-run during FindTextRuns.
  // Note: continuation frames must NOT add themselves; just the
  // first-in-flow
  nsresult AddText(nsIFrame* aTextFrame);

  // Close out a text-run during FindTextRuns.
  void EndTextRun();

  // This returns the first nsTextRun found during a FindTextRuns. The
  // internal text-run state is reset.
  nsTextRun* TakeTextRuns();

  nsIPresContext* mPresContext;

protected:
  // This state is constant for a given block frame doing line layout
  nsISpaceManager* mSpaceManager;
  const nsStyleText* mStyleText;
  const nsHTMLReflowState* mBlockReflowState;
  nsBlockReflowState* mBlockRS;/* XXX hack! */
  nscoord mMinLineHeight;
  PRPackedBool mComputeMaxElementSize;
  PRUint8 mTextAlign;

  // This state varies during the reflow of a line but is line
  // "global" state not span "local" state.
  nsIFrame* mFirstLetterFrame;
  PRInt32 mLineNumber;
  PRInt32 mColumn;
  PRInt32 mTextJustificationNumSpaces;
  PRInt32 mTextJustificationNumLetters;

  nsLineBox* mLineBox;

  PRUint8 mPlacedFloaters;
  PRInt32 mTotalPlacedFrames;
  nsVoidArray mWordFrames;

  nscoord mTopEdge;
  nscoord mBottomEdge;
  nscoord mMaxTopBoxHeight;
  nscoord mMaxBottomBoxHeight;
  nscoord mCarriedOutBottomMargin;

  // Final computed line-height value after VerticalAlignFrames for
  // the block has been called.
  nscoord mFinalLineHeight;

  nsTextRun* mReflowTextRuns;
  nsTextRun* mTextRun;

  // Per-frame data recorded by the line-layout reflow logic. This
  // state is the state needed to post-process the line after reflow
  // has completed (vertical alignment, horizontal alignment,
  // justification and relative positioning).

  struct PerSpanData;
  struct PerFrameData;
  friend struct PerSpanData;
  friend struct PerFrameData;
  struct PerFrameData {
    // link to next/prev frame in same span
    PerFrameData* mNext;
    PerFrameData* mPrev;

    // pointer to child span data if this is an inline container frame
    PerSpanData* mSpan;

    // The frame and its type
    nsIFrame* mFrame;
    nsCSSFrameType mFrameType;

    // From metrics
    nscoord mAscent, mDescent;
    nsRect mBounds;
    nsSize mMaxElementSize;
    nsRect mCombinedArea;

    // From reflow-state
    nsMargin mMargin;
    nsMargin mBorderPadding;
    nsMargin mOffsets;

    // Other state we use
    PRUint8 mVerticalAlign;

    // state for text justification
    PRInt32 mJustificationNumSpaces;
    PRInt32 mJustificationNumLetters;


// PerFrameData flags
#define PFD_RELATIVEPOS                 0x00000001
#define PFD_ISTEXTFRAME                 0x00000002
#define PFD_ISNONEMPTYTEXTFRAME         0x00000004
#define PFD_ISNONWHITESPACETEXTFRAME    0x00000008
#define PFD_ISLETTERFRAME               0x00000010
#define PFD_ISSTICKY                    0x00000020
#define PFD_ISBULLET                    0x00000040
#define PFD_LASTFLAG                    PFD_ISBULLET

    PRPackedBool mFlags;

    void SetFlag(PRUint32 aFlag, PRBool aValue)
    {
      NS_ASSERTION(aFlag<=PFD_LASTFLAG, "bad flag");
      NS_ASSERTION(aValue==PR_FALSE || aValue==PR_TRUE, "bad value");
      if (aValue) { // set flag
        mFlags |= aFlag;
      }
      else {        // unset flag
        mFlags &= ~aFlag;
      }
    }

    PRBool GetFlag(PRUint32 aFlag) const
    {
      NS_ASSERTION(aFlag<=PFD_LASTFLAG, "bad flag");
      PRBool result = (mFlags & aFlag);
      if (result) return PR_TRUE;
      return PR_FALSE;
    }


    PerFrameData* Last() {
      PerFrameData* pfd = this;
      while (pfd->mNext) {
        pfd = pfd->mNext;
      }
      return pfd;
    }
  };
  PerFrameData mFrameDataBuf[NS_LINELAYOUT_NUM_FRAMES];
  PerFrameData* mFrameFreeList;
  PRInt32 mInitialFramesFreed;

#ifdef AIX
public:
#endif
  struct PerSpanData {
    union {
      PerSpanData* mParent;
      PerSpanData* mNextFreeSpan;
    };
    PerFrameData* mFrame;
    PerFrameData* mFirstFrame;
    PerFrameData* mLastFrame;

    const nsHTMLReflowState* mReflowState;
    PRPackedBool mNoWrap;
    PRUint8 mDirection;
    PRPackedBool mChangedFrameDirection;
    PRPackedBool mZeroEffectiveSpanBox;
    PRPackedBool mContainsFloater;

    nscoord mLeftEdge;
    nscoord mX;
    nscoord mRightEdge;

    nscoord mTopLeading, mBottomLeading;
    nscoord mLogicalHeight;
    nscoord mMinY, mMaxY;

    void AppendFrame(PerFrameData* pfd) {
      if (nsnull == mLastFrame) {
        mFirstFrame = pfd;
      }
      else {
        mLastFrame->mNext = pfd;
        pfd->mPrev = mLastFrame;
      }
      mLastFrame = pfd;
    }
  };
#ifdef AIX
protected:
#endif
  PerSpanData mSpanDataBuf[NS_LINELAYOUT_NUM_SPANS];
  PerSpanData* mSpanFreeList;
  PRInt32 mInitialSpansFreed;
  PerSpanData* mRootSpan;
  PerSpanData* mCurrentSpan;
  PRInt32 mSpanDepth;
#ifdef DEBUG
  PRInt32 mSpansAllocated, mSpansFreed;
  PRInt32 mFramesAllocated, mFramesFreed;
#endif

  // XXX These slots are used ONLY during FindTextRuns
  nsTextRun* mTextRuns;
  nsTextRun** mTextRunP;
  nsTextRun* mNewTextRun;

  nsresult NewPerFrameData(PerFrameData** aResult);

  nsresult NewPerSpanData(PerSpanData** aResult);

  void FreeSpan(PerSpanData* psd);

  PRBool InBlockContext() const {
    return mSpanDepth == 0;
  }

  void PushFrame(nsIFrame* aFrame);

  void ApplyLeftMargin(PerFrameData* pfd,
                       nsHTMLReflowState& aReflowState);

  PRBool CanPlaceFrame(PerFrameData* pfd,
                       const nsHTMLReflowState& aReflowState,
                       PRBool aNotSafeToBreak,
                       nsHTMLReflowMetrics& aMetrics,
                       nsReflowStatus& aStatus);

  void PlaceFrame(PerFrameData* pfd,
                  nsHTMLReflowMetrics& aMetrics);

  void UpdateFrames();

  void VerticalAlignFrames(PerSpanData* psd);

  void PlaceTopBottomFrames(PerSpanData* psd,
                            nscoord aDistanceFromTop,
                            nscoord aLineHeight);

  void RelativePositionFrames(PerSpanData* psd, nsRect& aCombinedArea);

  PRBool TrimTrailingWhiteSpaceIn(PerSpanData* psd, nscoord* aDeltaWidth);


  void ComputeJustificationWeights(PerSpanData* psd, PRInt32* numSpaces, PRInt32* numLetters);

  struct FrameJustificationState {
    PRInt32 mTotalNumSpaces;
    PRInt32 mTotalNumLetters;
    nscoord mTotalWidthForSpaces;
    nscoord mTotalWidthForLetters;
    PRInt32 mNumSpacesProcessed;
    PRInt32 mNumLettersProcessed;
    nscoord mWidthForSpacesProcessed;
    nscoord mWidthForLettersProcessed;
  };
  nscoord ApplyFrameJustification(PerSpanData* aPSD, FrameJustificationState* aState);


#ifdef DEBUG
  void DumpPerSpanData(PerSpanData* psd, PRInt32 aIndent);
#endif
};

#endif /* nsLineLayout_h___ */
