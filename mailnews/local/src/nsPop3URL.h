/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#ifndef nsPop3URL_h__
#define nsPop3URL_h__

#include "nsIPop3URL.h"
#include "nsMsgMailNewsUrl.h"
#include "nsIMsgIncomingServer.h"
#include "nsCOMPtr.h"

class nsPop3URL : public nsIPop3URL, public nsMsgMailNewsUrl
{
public:
    NS_DECL_NSIPOP3URL
    nsPop3URL();
	
    NS_DECL_ISUPPORTS_INHERITED

protected:
	virtual ~nsPop3URL();
	// protocol specific code to parse a url...
    virtual nsresult ParseUrl(const nsString& aSpec);
	virtual const char * GetUserName() { return m_userName.GetBuffer();}

	nsCString m_userName;

	/* Pop3 specific event sinks */
    nsCOMPtr<nsIPop3Sink> m_pop3Sink;
};

#endif // nsPop3URL_h__
