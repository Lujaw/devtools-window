/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/ImageContainerChild.h"

#include "ImageContainer.h"
#include "GonkIOSurfaceImage.h"
#include "mozilla/ipc/Shmem.h"
#include "mozilla/ipc/CrossProcessMutex.h"
#include "SharedTextureImage.h"
#include "gfxImageSurface.h"
#include "gfxSharedImageSurface.h"
#include "yuv_convert.h"
#include "gfxUtils.h"

#ifdef XP_MACOSX
#include "mozilla/gfx/QuartzSupport.h"
#endif

#ifdef XP_WIN
#include "gfxD2DSurface.h"
#include "gfxWindowsPlatform.h"
#include <d3d10_1.h>

#include "d3d10/ImageLayerD3D10.h"
#endif

using namespace mozilla::ipc;
using mozilla::gfx::DataSourceSurface;
using mozilla::gfx::SourceSurface;


namespace mozilla {
namespace layers {

already_AddRefed<Image>
ImageFactory::CreateImage(const ImageFormat *aFormats,
                          PRUint32 aNumFormats,
                          const gfxIntSize &,
                          BufferRecycleBin *aRecycleBin)
{
  if (!aNumFormats) {
    return nullptr;
  }
  nsRefPtr<Image> img;
  if (FormatInList(aFormats, aNumFormats, PLANAR_YCBCR)) {
    img = new PlanarYCbCrImage(aRecycleBin);
  } else if (FormatInList(aFormats, aNumFormats, CAIRO_SURFACE)) {
    img = new CairoImage();
  } else if (FormatInList(aFormats, aNumFormats, SHARED_TEXTURE)) {
    img = new SharedTextureImage();
#ifdef XP_MACOSX
  } else if (FormatInList(aFormats, aNumFormats, MAC_IO_SURFACE)) {
    img = new MacIOSurfaceImage();
#endif
#ifdef MOZ_WIDGET_GONK
  } else if (FormatInList(aFormats, aNumFormats, GONK_IO_SURFACE)) {
    img = new GonkIOSurfaceImage();
#endif
  }
  return img.forget();
}

BufferRecycleBin::BufferRecycleBin()
  : mLock("mozilla.layers.BufferRecycleBin.mLock")
{
}

void
BufferRecycleBin::RecycleBuffer(PRUint8* aBuffer, PRUint32 aSize)
{
  MutexAutoLock lock(mLock);

  if (!mRecycledBuffers.IsEmpty() && aSize != mRecycledBufferSize) {
    mRecycledBuffers.Clear();
  }
  mRecycledBufferSize = aSize;
  mRecycledBuffers.AppendElement(aBuffer);
}

PRUint8*
BufferRecycleBin::GetBuffer(PRUint32 aSize)
{
  MutexAutoLock lock(mLock);

  if (mRecycledBuffers.IsEmpty() || mRecycledBufferSize != aSize)
    return new PRUint8[aSize];

  PRUint32 last = mRecycledBuffers.Length() - 1;
  PRUint8* result = mRecycledBuffers[last].forget();
  mRecycledBuffers.RemoveElementAt(last);
  return result;
}

ImageContainer::ImageContainer(int flag) 
: mReentrantMonitor("ImageContainer.mReentrantMonitor"),
  mPaintCount(0),
  mPreviousImagePainted(false),
  mImageFactory(new ImageFactory()),
  mRecycleBin(new BufferRecycleBin()),
  mRemoteData(nullptr),
  mRemoteDataMutex(nullptr),
  mCompositionNotifySink(nullptr),
  mImageContainerChild(nullptr)
{
  if (flag == ENABLE_ASYNC && ImageBridgeChild::IsCreated()) {
    mImageContainerChild = 
      ImageBridgeChild::GetSingleton()->CreateImageContainerChild();
  }
}

ImageContainer::~ImageContainer()
{
  if (mImageContainerChild) {
    mImageContainerChild->DispatchStop();
  }
}

already_AddRefed<Image>
ImageContainer::CreateImage(const ImageFormat *aFormats,
                            PRUint32 aNumFormats)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  return mImageFactory->CreateImage(aFormats, aNumFormats, mScaleHint, mRecycleBin);
}

void 
ImageContainer::SetCurrentImageInternal(Image *aImage)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (mRemoteData) {
    NS_ASSERTION(mRemoteDataMutex, "Should have remote data mutex when having remote data!");
    mRemoteDataMutex->Lock();
    // This is important since it ensures we won't change the active image
    // when we currently have a locked image that depends on mRemoteData.
  }

  mActiveImage = aImage;
  CurrentImageChanged();

  if (mRemoteData) {
    mRemoteDataMutex->Unlock();
  }
}

void
ImageContainer::SetCurrentImage(Image *aImage)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (mImageContainerChild) {
    if (aImage) {
      mImageContainerChild->SendImageAsync(this, aImage);
    } else {
      mImageContainerChild->DispatchSetIdle();
    }
  }
  
  SetCurrentImageInternal(aImage);
}

void
ImageContainer::SetCurrentImageInTransaction(Image *aImage)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  NS_ASSERTION(!mImageContainerChild, "Should use async image transfer with ImageBridge.");
  
  SetCurrentImageInternal(aImage);
}

bool ImageContainer::IsAsync() const {
  return mImageContainerChild != nullptr;
}

PRUint64 ImageContainer::GetAsyncContainerID() const
{
  NS_ASSERTION(IsAsync(),"Shared image ID is only relevant to async ImageContainers");
  if (IsAsync()) {
    return mImageContainerChild->GetID();
  } else {
    return 0; // zero is always an invalid SharedImageID
  }
}

bool
ImageContainer::HasCurrentImage()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (mRemoteData) {
    CrossProcessMutexAutoLock autoLock(*mRemoteDataMutex);
    
    EnsureActiveImage();

    return !!mActiveImage.get();
  }

  return !!mActiveImage.get();
}

already_AddRefed<Image>
ImageContainer::LockCurrentImage()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  
  if (mRemoteData) {
    NS_ASSERTION(mRemoteDataMutex, "Should have remote data mutex when having remote data!");
    mRemoteDataMutex->Lock();
  }

  EnsureActiveImage();

  nsRefPtr<Image> retval = mActiveImage;
  return retval.forget();
}

already_AddRefed<gfxASurface>
ImageContainer::LockCurrentAsSurface(gfxIntSize *aSize, Image** aCurrentImage)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (mRemoteData) {
    NS_ASSERTION(mRemoteDataMutex, "Should have remote data mutex when having remote data!");
    mRemoteDataMutex->Lock();

    EnsureActiveImage();

    if (aCurrentImage) {
      NS_IF_ADDREF(mActiveImage);
      *aCurrentImage = mActiveImage.get();
    }

    if (!mActiveImage) {
      return nullptr;
    } 

    if (mActiveImage->GetFormat() == REMOTE_IMAGE_BITMAP) {
      nsRefPtr<gfxImageSurface> newSurf =
        new gfxImageSurface(mRemoteData->mBitmap.mData, mRemoteData->mSize, mRemoteData->mBitmap.mStride,
                            mRemoteData->mFormat == RemoteImageData::BGRX32 ?
                                                   gfxASurface::ImageFormatARGB32 :
                                                   gfxASurface::ImageFormatRGB24);

      *aSize = newSurf->GetSize();
    
      return newSurf.forget();
    }

    *aSize = mActiveImage->GetSize();
    return mActiveImage->GetAsSurface();
  }

  if (aCurrentImage) {
    NS_IF_ADDREF(mActiveImage);
    *aCurrentImage = mActiveImage.get();
  }

  if (!mActiveImage) {
    return nullptr;
  }

  *aSize = mActiveImage->GetSize();
  return mActiveImage->GetAsSurface();
}

void
ImageContainer::UnlockCurrentImage()
{
  if (mRemoteData) {
    NS_ASSERTION(mRemoteDataMutex, "Should have remote data mutex when having remote data!");
    mRemoteDataMutex->Unlock();
  }
}

already_AddRefed<gfxASurface>
ImageContainer::GetCurrentAsSurface(gfxIntSize *aSize)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (mRemoteData) {
    CrossProcessMutexAutoLock autoLock(*mRemoteDataMutex);
    EnsureActiveImage();

    if (!mActiveImage)
      return nullptr;
    *aSize = mRemoteData->mSize;
  } else {
    if (!mActiveImage)
      return nullptr;
    *aSize = mActiveImage->GetSize();
  }
  return mActiveImage->GetAsSurface();
}

gfxIntSize
ImageContainer::GetCurrentSize()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (mRemoteData) {
    CrossProcessMutexAutoLock autoLock(*mRemoteDataMutex);

    // We don't need to ensure we have an active image here, as we need to
    // be in the mutex anyway, and this is easiest to return from there.
    return mRemoteData->mSize;
  }

  if (!mActiveImage) {
    return gfxIntSize(0,0);
  }

  return mActiveImage->GetSize();
}

void
ImageContainer::SetRemoteImageData(RemoteImageData *aData, CrossProcessMutex *aMutex)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  NS_ASSERTION(!mActiveImage || !aData, "No active image expected when SetRemoteImageData is called with non-NULL aData.");
  NS_ASSERTION(!mRemoteData || !aData, "No remote data expected when SetRemoteImageData is called with non-NULL aData.");

  mRemoteData = aData;

  if (aData) {
    memset(aData, 0, sizeof(RemoteImageData));
  } else {
    mActiveImage = nullptr;
  }

  mRemoteDataMutex = aMutex;
}

void
ImageContainer::EnsureActiveImage()
{
  if (mRemoteData) {
    if (mRemoteData->mWasUpdated) {
      mActiveImage = nullptr;
    }

    if (mRemoteData->mType == RemoteImageData::RAW_BITMAP &&
        mRemoteData->mBitmap.mData && !mActiveImage) {
      nsRefPtr<RemoteBitmapImage> newImg = new RemoteBitmapImage();
      
      newImg->mFormat = mRemoteData->mFormat;
      newImg->mData = mRemoteData->mBitmap.mData;
      newImg->mSize = mRemoteData->mSize;
      newImg->mStride = mRemoteData->mBitmap.mStride;
      mRemoteData->mWasUpdated = false;
              
      mActiveImage = newImg;
    }
#ifdef XP_WIN
    else if (mRemoteData->mType == RemoteImageData::DXGI_TEXTURE_HANDLE &&
             mRemoteData->mTextureHandle && !mActiveImage) {
      nsRefPtr<RemoteDXGITextureImage> newImg = new RemoteDXGITextureImage();
      newImg->mSize = mRemoteData->mSize;
      newImg->mHandle = mRemoteData->mTextureHandle;
      newImg->mFormat = mRemoteData->mFormat;
      mRemoteData->mWasUpdated = false;

      mActiveImage = newImg;
    }
#endif
  }
}


PlanarYCbCrImage::PlanarYCbCrImage(BufferRecycleBin *aRecycleBin)
  : Image(nullptr, PLANAR_YCBCR)
  , mBufferSize(0)
  , mRecycleBin(aRecycleBin)
{
}

PlanarYCbCrImage::~PlanarYCbCrImage()
{
  if (mBuffer) {
    mRecycleBin->RecycleBuffer(mBuffer.forget(), mBufferSize);
  }
}

PRUint8* 
PlanarYCbCrImage::AllocateBuffer(PRUint32 aSize)
{
  return mRecycleBin->GetBuffer(aSize); 
}

static void
CopyPlane(PRUint8 *aDst, PRUint8 *aSrc,
          const gfxIntSize &aSize, PRInt32 aStride,
          PRInt32 aOffset, PRInt32 aSkip)
{
  if (!aOffset && !aSkip) {
    // Fast path: planar input.
    memcpy(aDst, aSrc, aSize.height * aStride);
  } else {
    PRInt32 height = aSize.height;
    PRInt32 width = aSize.width;
    for (int y = 0; y < height; ++y) {
      PRUint8 *src = aSrc + aOffset;
      PRUint8 *dst = aDst;
      if (!aSkip) {
        // Fast path: offset only, no per-pixel skip.
        memcpy(dst, src, width);
      } else {
        // Slow path
        for (int x = 0; x < width; ++x) {
          *dst++ = *src++;
          src += aSkip;
        }
      }
      aSrc += aStride;
      aDst += aStride;
    }
  }
}

void
PlanarYCbCrImage::CopyData(const Data& aData,
                           PRInt32 aYOffset, PRInt32 aYSkip,
                           PRInt32 aCbOffset, PRInt32 aCbSkip,
                           PRInt32 aCrOffset, PRInt32 aCrSkip)
{
  mData = aData;

  // update buffer size
  mBufferSize = mData.mCbCrStride * mData.mCbCrSize.height * 2 +
                mData.mYStride * mData.mYSize.height;

  // get new buffer
  mBuffer = AllocateBuffer(mBufferSize); 
  if (!mBuffer)
    return;

  mData.mYChannel = mBuffer;
  mData.mCbChannel = mData.mYChannel + mData.mYStride * mData.mYSize.height;
  mData.mCrChannel = mData.mCbChannel + mData.mCbCrStride * mData.mCbCrSize.height;

  CopyPlane(mData.mYChannel, aData.mYChannel,
            mData.mYSize, mData.mYStride,
            aYOffset, aYSkip);
  CopyPlane(mData.mCbChannel, aData.mCbChannel,
            mData.mCbCrSize, mData.mCbCrStride,
            aCbOffset, aCbSkip);
  CopyPlane(mData.mCrChannel, aData.mCrChannel,
            mData.mCbCrSize, mData.mCbCrStride,
            aCrOffset, aCrSkip);

  mSize = aData.mPicSize;
}

void
PlanarYCbCrImage::SetData(const Data &aData)
{
  CopyData(aData);
}

already_AddRefed<gfxASurface>
PlanarYCbCrImage::GetAsSurface()
{
  if (mSurface) {
    nsRefPtr<gfxASurface> result = mSurface.get();
    return result.forget();
  }

  gfxASurface::gfxImageFormat format = GetOffscreenFormat();

  gfxIntSize size(mSize);
  gfxUtils::GetYCbCrToRGBDestFormatAndSize(mData, format, size);
  if (size.width > PlanarYCbCrImage::MAX_DIMENSION ||
      size.height > PlanarYCbCrImage::MAX_DIMENSION) {
    NS_ERROR("Illegal image dest width or height");
    return nullptr;
  }

  nsRefPtr<gfxImageSurface> imageSurface =
    new gfxImageSurface(mSize, format);

  gfxUtils::ConvertYCbCrToRGB(mData, format, mSize,
                              imageSurface->Data(),
                              imageSurface->Stride());

  mSurface = imageSurface;

  return imageSurface.forget().get();
}

#ifdef XP_MACOSX
void
MacIOSurfaceImage::SetData(const Data& aData)
{
  mIOSurface = MacIOSurface::LookupSurface(aData.mIOSurface->GetIOSurfaceID());
  mSize = gfxIntSize(mIOSurface->GetWidth(), mIOSurface->GetHeight());
}

already_AddRefed<gfxASurface>
MacIOSurfaceImage::GetAsSurface()
{
  mIOSurface->Lock();
  size_t bytesPerRow = mIOSurface->GetBytesPerRow();
  size_t ioWidth = mIOSurface->GetWidth();
  size_t ioHeight = mIOSurface->GetHeight();

  unsigned char* ioData = (unsigned char*)mIOSurface->GetBaseAddress();

  nsRefPtr<gfxImageSurface> imgSurface =
    new gfxImageSurface(gfxIntSize(ioWidth, ioHeight), gfxASurface::ImageFormatARGB32);

  for (int i = 0; i < ioHeight; i++) {
    memcpy(imgSurface->Data() + i * imgSurface->Stride(),
           ioData + i * bytesPerRow, ioWidth * 4);
  }

  mIOSurface->Unlock();

  return imgSurface.forget();
}

void
MacIOSurfaceImage::Update(ImageContainer* aContainer)
{
  if (mUpdateCallback) {
    mUpdateCallback(aContainer, mPluginInstanceOwner);
  }
}
#endif

already_AddRefed<gfxASurface>
RemoteBitmapImage::GetAsSurface()
{
  nsRefPtr<gfxImageSurface> newSurf =
    new gfxImageSurface(mSize,
    mFormat == RemoteImageData::BGRX32 ? gfxASurface::ImageFormatRGB24 : gfxASurface::ImageFormatARGB32);

  for (int y = 0; y < mSize.height; y++) {
    memcpy(newSurf->Data() + newSurf->Stride() * y,
           mData + mStride * y,
           mSize.width * 4);
  }

  return newSurf.forget();
}

} // namespace
} // namespace
