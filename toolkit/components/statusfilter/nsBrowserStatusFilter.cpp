/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsBrowserStatusFilter.h"
#include "nsIChannel.h"
#include "nsITimer.h"
#include "nsIServiceManager.h"
#include "nsString.h"

// XXX
// XXX  DO NOT TOUCH THIS CODE UNLESS YOU KNOW WHAT YOU'RE DOING !!!
// XXX

//-----------------------------------------------------------------------------
// nsBrowserStatusFilter <public>
//-----------------------------------------------------------------------------

nsBrowserStatusFilter::nsBrowserStatusFilter()
    : mCurProgress(0)
    , mMaxProgress(0)
    , mStatusIsDirty(true)
    , mCurrentPercentage(0)
    , mTotalRequests(0)
    , mFinishedRequests(0)
    , mUseRealProgressFlag(false)
    , mDelayedStatus(false)
    , mDelayedProgress(false)
{
}

nsBrowserStatusFilter::~nsBrowserStatusFilter()
{
    if (mTimer) {
        mTimer->Cancel();
    }
}

//-----------------------------------------------------------------------------
// nsBrowserStatusFilter::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS4(nsBrowserStatusFilter,
                   nsIWebProgress,
                   nsIWebProgressListener,
                   nsIWebProgressListener2,
                   nsISupportsWeakReference)

//-----------------------------------------------------------------------------
// nsBrowserStatusFilter::nsIWebProgress
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsBrowserStatusFilter::AddProgressListener(nsIWebProgressListener *aListener,
                                           PRUint32 aNotifyMask)
{
    mListener = aListener;
    return NS_OK;
}

NS_IMETHODIMP
nsBrowserStatusFilter::RemoveProgressListener(nsIWebProgressListener *aListener)
{
    if (aListener == mListener)
        mListener = nullptr;
    return NS_OK;
}

NS_IMETHODIMP
nsBrowserStatusFilter::GetDOMWindow(nsIDOMWindow **aResult)
{
    NS_NOTREACHED("nsBrowserStatusFilter::GetDOMWindow");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsBrowserStatusFilter::GetIsLoadingDocument(bool *aIsLoadingDocument)
{
    NS_NOTREACHED("nsBrowserStatusFilter::GetIsLoadingDocument");
    return NS_ERROR_NOT_IMPLEMENTED;
}


//-----------------------------------------------------------------------------
// nsBrowserStatusFilter::nsIWebProgressListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsBrowserStatusFilter::OnStateChange(nsIWebProgress *aWebProgress,
                                     nsIRequest *aRequest,
                                     PRUint32 aStateFlags,
                                     nsresult aStatus)
{
    if (!mListener)
        return NS_OK;

    if (aStateFlags & STATE_START) {
        if (aStateFlags & STATE_IS_NETWORK) {
            ResetMembers();
        }
        if (aStateFlags & STATE_IS_REQUEST) {
            ++mTotalRequests;

            // if the total requests exceeds 1, then we'll base our progress
            // notifications on the percentage of completed requests.
            // otherwise, progress for the single request will be reported.
            mUseRealProgressFlag = (mTotalRequests == 1);
        }
    }
    else if (aStateFlags & STATE_STOP) {
        if (aStateFlags & STATE_IS_REQUEST) {
            ++mFinishedRequests;
            // Note: Do not return from here. This is necessary so that the
            // STATE_STOP can still be relayed to the listener if needed
            // (bug 209330)
            if (!mUseRealProgressFlag && mTotalRequests)
                OnProgressChange(nullptr, nullptr, 0, 0,
                                 mFinishedRequests, mTotalRequests);
        }
    }
    else if (aStateFlags & STATE_TRANSFERRING) {
        if (aStateFlags & STATE_IS_REQUEST) {
            if (!mUseRealProgressFlag && mTotalRequests)
                return OnProgressChange(nullptr, nullptr, 0, 0,
                                        mFinishedRequests, mTotalRequests);
        }

        // no need to forward this state change
        return NS_OK;
    } else {
        // no need to forward this state change
        return NS_OK;
    }

    // If we're here, we have either STATE_START or STATE_STOP.  The
    // listener only cares about these in certain conditions.
    bool isLoadingDocument = false;
    if ((aStateFlags & nsIWebProgressListener::STATE_IS_NETWORK ||
         (aStateFlags & nsIWebProgressListener::STATE_IS_REQUEST &&
          mFinishedRequests == mTotalRequests &&
          (aWebProgress->GetIsLoadingDocument(&isLoadingDocument),
           !isLoadingDocument)))) {
        if (mTimer && (aStateFlags & nsIWebProgressListener::STATE_STOP)) {
            mTimer->Cancel();
            ProcessTimeout();
        }

        return mListener->OnStateChange(aWebProgress, aRequest, aStateFlags,
                                        aStatus);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsBrowserStatusFilter::OnProgressChange(nsIWebProgress *aWebProgress,
                                        nsIRequest *aRequest,
                                        PRInt32 aCurSelfProgress,
                                        PRInt32 aMaxSelfProgress,
                                        PRInt32 aCurTotalProgress,
                                        PRInt32 aMaxTotalProgress)
{
    if (!mListener)
        return NS_OK;

    if (!mUseRealProgressFlag && aRequest)
        return NS_OK;

    //
    // limit frequency of calls to OnProgressChange
    //

    mCurProgress = (PRInt64)aCurTotalProgress;
    mMaxProgress = (PRInt64)aMaxTotalProgress;

    if (mDelayedProgress)
        return NS_OK;

    if (!mDelayedStatus) {
        MaybeSendProgress();
        StartDelayTimer();
    }

    mDelayedProgress = true;

    return NS_OK;
}

NS_IMETHODIMP
nsBrowserStatusFilter::OnLocationChange(nsIWebProgress *aWebProgress,
                                        nsIRequest *aRequest,
                                        nsIURI *aLocation,
                                        PRUint32 aFlags)
{
    if (!mListener)
        return NS_OK;

    return mListener->OnLocationChange(aWebProgress, aRequest, aLocation,
                                       aFlags);
}

NS_IMETHODIMP
nsBrowserStatusFilter::OnStatusChange(nsIWebProgress *aWebProgress,
                                      nsIRequest *aRequest,
                                      nsresult aStatus,
                                      const PRUnichar *aMessage)
{
    if (!mListener)
        return NS_OK;

    //
    // limit frequency of calls to OnStatusChange
    //
    if (mStatusIsDirty || !mCurrentStatusMsg.Equals(aMessage)) {
        mStatusIsDirty = true;
        mStatusMsg = aMessage;
    }

    if (mDelayedStatus)
        return NS_OK;

    if (!mDelayedProgress) {
      MaybeSendStatus();
      StartDelayTimer();
    }

    mDelayedStatus = true;

    return NS_OK;
}

NS_IMETHODIMP
nsBrowserStatusFilter::OnSecurityChange(nsIWebProgress *aWebProgress,
                                        nsIRequest *aRequest,
                                        PRUint32 aState)
{
    if (!mListener)
        return NS_OK;

    return mListener->OnSecurityChange(aWebProgress, aRequest, aState);
}

//-----------------------------------------------------------------------------
// nsBrowserStatusFilter::nsIWebProgressListener2
//-----------------------------------------------------------------------------
NS_IMETHODIMP
nsBrowserStatusFilter::OnProgressChange64(nsIWebProgress *aWebProgress,
                                          nsIRequest *aRequest,
                                          PRInt64 aCurSelfProgress,
                                          PRInt64 aMaxSelfProgress,
                                          PRInt64 aCurTotalProgress,
                                          PRInt64 aMaxTotalProgress)
{
    // XXX truncates 64-bit to 32-bit
    return OnProgressChange(aWebProgress, aRequest,
                            (PRInt32)aCurSelfProgress,
                            (PRInt32)aMaxSelfProgress,
                            (PRInt32)aCurTotalProgress,
                            (PRInt32)aMaxTotalProgress);
}

NS_IMETHODIMP
nsBrowserStatusFilter::OnRefreshAttempted(nsIWebProgress *aWebProgress,
                                          nsIURI *aUri,
                                          PRInt32 aDelay,
                                          bool aSameUri,
                                          bool *allowRefresh)
{
    nsCOMPtr<nsIWebProgressListener2> listener =
        do_QueryInterface(mListener);
    if (!listener) {
        *allowRefresh = true;
        return NS_OK;
    }

    return listener->OnRefreshAttempted(aWebProgress, aUri, aDelay, aSameUri,
                                        allowRefresh);
}

//-----------------------------------------------------------------------------
// nsBrowserStatusFilter <private>
//-----------------------------------------------------------------------------

void
nsBrowserStatusFilter::ResetMembers()
{
    mTotalRequests = 0;
    mFinishedRequests = 0;
    mUseRealProgressFlag = false;
    mMaxProgress = 0;
    mCurProgress = 0;
    mCurrentPercentage = 0;
    mStatusIsDirty = true;
}

void
nsBrowserStatusFilter::MaybeSendProgress() 
{
    if (mCurProgress > mMaxProgress || mCurProgress <= 0) 
        return;

    // check our percentage
    PRInt32 percentage = (PRInt32) double(mCurProgress) * 100 / mMaxProgress;

    // The progress meter only updates for increases greater than 3 percent
    if (percentage > (mCurrentPercentage + 3)) {
        mCurrentPercentage = percentage;
        // XXX truncates 64-bit to 32-bit
        mListener->OnProgressChange(nullptr, nullptr, 0, 0,
                                    (PRInt32)mCurProgress,
                                    (PRInt32)mMaxProgress);
    }
}

void
nsBrowserStatusFilter::MaybeSendStatus()
{
    if (mStatusIsDirty) {
        mListener->OnStatusChange(nullptr, nullptr, NS_OK, mStatusMsg.get());
        mCurrentStatusMsg = mStatusMsg;
        mStatusIsDirty = false;
    }
}

nsresult
nsBrowserStatusFilter::StartDelayTimer()
{
    NS_ASSERTION(!DelayInEffect(), "delay should not be in effect");

    mTimer = do_CreateInstance("@mozilla.org/timer;1");
    if (!mTimer)
        return NS_ERROR_FAILURE;

    return mTimer->InitWithFuncCallback(TimeoutHandler, this, 160, 
                                        nsITimer::TYPE_ONE_SHOT);
}

void
nsBrowserStatusFilter::ProcessTimeout()
{
    mTimer = nullptr;

    if (!mListener)
        return;

    if (mDelayedStatus) {
        mDelayedStatus = false;
        MaybeSendStatus();
    }

    if (mDelayedProgress) {
        mDelayedProgress = false;
        MaybeSendProgress();
    }
}

void
nsBrowserStatusFilter::TimeoutHandler(nsITimer *aTimer, void *aClosure)
{
    nsBrowserStatusFilter *self = reinterpret_cast<nsBrowserStatusFilter *>(aClosure);
    if (!self) {
        NS_ERROR("no self");
        return;
    }

    self->ProcessTimeout();
}
