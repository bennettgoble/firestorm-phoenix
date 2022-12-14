/**
 * @file exoflickrauth.cpp
 * @brief Handles all Flickr authentication issues
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Copyright (C) 2012 Katharine Berry
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "fscorehttputil.h"
#include "llwindow.h"
#include "llviewerwindow.h"
#include "llbufferstream.h"
#include "lluri.h"
#include "llviewercontrol.h"
#include "llnotificationsutil.h"
#include "exoflickr.h"
#include "llcorehttputil.h"

#include "exoflickrauth.h"

bool exoFlickrAuth::sAuthorisationInProgress = false;

void exoFlickrAuthResponse( LLSD const &aData, exoFlickrAuth::authorized_callback_t aCallback )
{
    LLSD header = aData[ LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS ][ LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_HEADERS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD( aData[ LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS ] );

    const LLSD::Binary &rawData = aData[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
    std::string result;
    result.assign( rawData.begin(), rawData.end() );

    LLSD resultLLSD = LLURI::queryMap( result );
    aCallback((status.getType() == HTTP_OK), resultLLSD);
}


exoFlickrAuth::exoFlickrAuth(authorized_callback_t callback)
: mCallback(callback)
{
    // Avoid doubled authentication attempts.
    if(sAuthorisationInProgress)
    {
        delete this;
        return;
    }
    mAuthenticating = true;
    sAuthorisationInProgress = true;
    if(gSavedPerAccountSettings.getString("ExodusFlickrToken").empty() || gSavedPerAccountSettings.getString("ExodusFlickrTokenSecret").empty())
    {
        beginAuthorisation();
    }
    else
    {
        checkAuthorisation();
    }
}

exoFlickrAuth::~exoFlickrAuth()
{
    if(mAuthenticating)
    {
        sAuthorisationInProgress = false;
    }
}

void exoFlickrAuth::checkAuthorisation()
{
    exoFlickr::request("flickr.test.login", LLSD(), boost::bind(&exoFlickrAuth::checkResult, this, _1, _2));
}

void exoFlickrAuth::checkResult(bool success, const LLSD &response)
{
    if(!success)
    {
        LL_WARNS("Flickr") << "Login test failed (HTTP). Reauthenticating." << LL_ENDL;
        beginAuthorisation();
    }
    else
    {
        if(response["stat"].asString() != "ok")
        {
            LL_WARNS("Flickr") << "Login test failed: " << response["message"] << "(" << response["code"] << "). Reauthenticating." << LL_ENDL;
            beginAuthorisation();
        }
        else
        {
            LL_INFOS("Flickr") << "Stored Flickr tokens are valid." << LL_ENDL;
            mCallback(true, LLSD());
            delete this;
            return;
        }
    }
}


void exoFlickrAuth::beginAuthorisation()
{
    // Explain and confirm the process to the user.
    LLNotificationsUtil::add("ExodusFlickrVerificationExplanation", LLSD(), LLSD(), boost::bind(&exoFlickrAuth::explanationCallback, this, _1, _2));
}

void exoFlickrAuth::explanationCallback(const LLSD& notification, const LLSD& response)
{
    int option = LLNotificationsUtil::getSelectedOption(notification, response);
    if(option == 0) // OK
    {
        // Clear out any old authentication tokens
        gSavedPerAccountSettings.setString("ExodusFlickrToken", "");
        gSavedPerAccountSettings.setString("ExodusFlickrTokenSecret","");

        // Initiate authentication step one.
        LLSD params;
        params["oauth_callback"] = "oob";
        LL_INFOS("Flickr") << "Initialising OAuth authorisation process." << LL_ENDL;
        exoFlickr::signRequest(params, "GET", "https://www.flickr.com/services/oauth/request_token");

        std::string url = LLURI::buildHTTP( "https://www.flickr.com/services/oauth/request_token", LLSD::emptyArray(), params ).asString();

        exoFlickrAuth::authorized_callback_t callback = boost::bind(&exoFlickrAuth::gotRequestToken, this, _1, _2);
        FSCoreHttpUtil::callbackHttpGetRaw( url, boost::bind( exoFlickrAuthResponse, _1, callback ) );
    }
    else
    {
        mCallback(false, LLSD());
        delete this;
        return;
    }
}

void exoFlickrAuth::gotRequestToken(bool success, const LLSD& params)
{
    LL_INFOS("Flickr") << "Got request token, success = " << success << LL_ENDL;
    if(!success)
    {
        mCallback(false, LLSD());
        delete this;
        return;
    }
    std::string token = params["oauth_token"].asString();
    std::string secret = params["oauth_token_secret"].asString();
    gSavedPerAccountSettings.setString("ExodusFlickrToken", token);
    gSavedPerAccountSettings.setString("ExodusFlickrTokenSecret", secret);
    LL_INFOS("Flickr") << "Token = '" << token << "', secret = '" << secret
                << "'" << LL_ENDL;

    // Proceed with stage two.
    // We spawn the browser using spawnWebBrowser instead of LLWeb to bypass the
    // browser prompt. This would a) be redundant to our earlier notice, and b)
    // is unclickable because we have also presented a modal dialog.
    gViewerWindow->getWindow()->spawnWebBrowser("https://www.flickr.com/services/oauth/authorize?perms=write&oauth_token=" + token, true);
    LLNotificationsUtil::add("ExodusFlickrVerificationPrompt", LLSD(), LLSD(), boost::bind(&exoFlickrAuth::gotVerifier, this, _1, _2));
}

void exoFlickrAuth::gotVerifier(const LLSD& notification, const LLSD& response)
{
    int option = LLNotificationsUtil::getSelectedOption(notification, response);
    if(option == 1) // Cancel
    {
        mCallback(false, LLSD());
        delete this;
        return;
    }

    // Proceed with stage three.
    LLSD params;
    params["oauth_verifier"] = response["oauth_verifier"];
    exoFlickr::signRequest(params, "GET", "https://www.flickr.com/services/oauth/access_token");

    std::string url = LLURI::buildHTTP( "https://www.flickr.com/services/oauth/access_token", LLSD::emptyArray(), params ).asString();

    exoFlickrAuth::authorized_callback_t callback = boost::bind(&exoFlickrAuth::gotAccessToken, this, _1, _2);
    FSCoreHttpUtil::callbackHttpGetRaw( url, boost::bind( exoFlickrAuthResponse, _1, callback ) );

}

void exoFlickrAuth::gotAccessToken(bool success, const LLSD& params)
{
    if(success)
    {
        // Save all the information we got back.
        gSavedPerAccountSettings.setString("ExodusFlickrToken", params["oauth_token"].asString());
        gSavedPerAccountSettings.setString("ExodusFlickrTokenSecret", params["oauth_token_secret"].asString());
        gSavedPerAccountSettings.setString("ExodusFlickrFullName", params["fullname"].asString());
        gSavedPerAccountSettings.setString("ExodusFlickrNSID", params["user_nsid"].asString());
        gSavedPerAccountSettings.setString("ExodusFlickrUsername", params["username"].asString());
        mCallback(true, params);
    }
    else
    {
        LLNotificationsUtil::add("ExodusFlickrVerificationFailed");
        mCallback(false, params);
    }

    delete this;
}
