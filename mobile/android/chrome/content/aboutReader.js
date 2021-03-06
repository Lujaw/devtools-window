/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

let Ci = Components.interfaces, Cc = Components.classes, Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm")
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyGetter(window, "gChromeWin", function ()
  window.QueryInterface(Ci.nsIInterfaceRequestor)
    .getInterface(Ci.nsIWebNavigation)
    .QueryInterface(Ci.nsIDocShellTreeItem)
    .rootTreeItem
    .QueryInterface(Ci.nsIInterfaceRequestor)
    .getInterface(Ci.nsIDOMWindow)
    .QueryInterface(Ci.nsIDOMChromeWindow));

function dump(s) {
  Services.console.logStringMessage("Reader: " + s);
}

let gStrings = Services.strings.createBundle("chrome://browser/locale/aboutReader.properties");

let AboutReader = {
  _STEP_INCREMENT: 0,
  _STEP_DECREMENT: 1,

  _BLOCK_IMAGES_SELECTOR: ".content p > img:only-child, " +
                          ".content p > a:only-child > img:only-child, " +
                          ".content .wp-caption img, " +
                          ".content figure img",

  init: function Reader_init() {
    dump("Init()");

    Services.obs.addObserver(this, "Reader:FaviconReturn", false);

    this._article = null;

    dump("Feching toolbar, header and content notes from about:reader");
    this._headerElement = document.getElementById("reader-header");
    this._domainElement = document.getElementById("reader-domain");
    this._titleElement = document.getElementById("reader-title");
    this._creditsElement = document.getElementById("reader-credits");
    this._contentElement = document.getElementById("reader-content");
    this._toolbarElement = document.getElementById("reader-toolbar");

    this._toolbarEnabled = false;

    this._scrollOffset = window.pageYOffset;

    let body = document.body;
    body.addEventListener("touchstart", this, false);
    body.addEventListener("click", this, false);
    window.addEventListener("scroll", this, false);
    window.addEventListener("popstate", this, false);
    window.addEventListener("resize", this, false);

    this._setupAllDropdowns();
    this._setupButton("toggle-button", this._onReaderToggle.bind(this));
    this._setupButton("list-button", this._onList.bind(this));
    this._setupButton("share-button", this._onShare.bind(this));

    let colorSchemeOptions = [
      { name: gStrings.GetStringFromName("aboutReader.colorSchemeLight"),
        value: "light"},
      { name: gStrings.GetStringFromName("aboutReader.colorSchemeDark"),
        value: "dark"}
    ];

    let colorScheme = Services.prefs.getCharPref("reader.color_scheme");
    this._setupSegmentedButton("color-scheme-buttons", colorSchemeOptions, colorScheme, this._setColorScheme.bind(this));
    this._setColorScheme(colorScheme);

    let fontTitle = gStrings.GetStringFromName("aboutReader.textTitle");
    this._setupStepControl("font-size-control", fontTitle, this._onFontSizeChange.bind(this));
    this._fontSize = 0;
    this._setFontSize(Services.prefs.getIntPref("reader.font_size"));

    let marginTitle = gStrings.GetStringFromName("aboutReader.marginTitle");
    this._setupStepControl("margin-size-control", marginTitle, this._onMarginSizeChange.bind(this));
    this._marginSize = 0;
    this._setMarginSize(Services.prefs.getIntPref("reader.margin_size"));

    dump("Decoding query arguments");
    let queryArgs = this._decodeQueryString(window.location.href);

    this._isReadingListItem = (queryArgs.readingList == "1");
    this._updateToggleButton();

    let url = queryArgs.url;
    let tabId = queryArgs.tabId;
    if (tabId) {
      dump("Loading from tab with ID: " + tabId + ", URL: " + url);
      this._loadFromTab(tabId, url);
    } else {
      dump("Fetching page with URL: " + url);
      this._loadFromURL(url);
    }
  },

  observe: function(aMessage, aTopic, aData) {
    switch(aTopic) {
      case "Reader:FaviconReturn": {
        let info = JSON.parse(aData);
        this._loadFavicon(info.url, info.faviconUrl);
        break;
      }
    }
  },

  handleEvent: function Reader_handleEvent(aEvent) {
    switch (aEvent.type) {
      case "touchstart":
        this._scrolled = false;
        break;
      case "click":
        if (!this._scrolled)
          this._toggleToolbarVisibility();
        break;
      case "scroll":
        if (!this._scrolled) {
          this._scrolled = true;
          this._setToolbarVisibility(false);
        }
        break;
      case "popstate":
        if (!aEvent.state)
          this._closeAllDropdowns();
        break;
      case "resize":
        this._updateImageMargins();
        break;
    }
  },

  uninit: function Reader_uninit() {
    dump("Uninit()");

    Services.obs.removeObserver(this, "Reader:FaviconReturn", false);

    let body = document.body;
    body.removeEventListener("touchstart", this, false);
    body.removeEventListener("click", this, false);
    window.removeEventListener("scroll", this, false);
    window.removeEventListener("popstate", this, false);
    window.removeEventListener("resize", this, false);

    this._hideContent();
  },

  _updateToggleButton: function Reader_updateToggleButton() {
    let classes = document.getElementById("toggle-button").classList;

    if (this._isReadingListItem) {
      classes.add("on");
    } else {
      classes.remove("on");
    }
  },

  _onReaderToggle: function Reader_onToggle() {
    if (!this._article)
      return;

    this._isReadingListItem = !this._isReadingListItem;
    this._updateToggleButton();

    if (this._isReadingListItem) {
      gChromeWin.Reader.storeArticleInCache(this._article, function(success) {
        dump("Reader:Add (in reader) success=" + success);

        gChromeWin.sendMessageToJava({
          gecko: {
            type: "Reader:Added",
            success: success,
            title: this._article.title,
            url: this._article.url,
          }
        });
      }.bind(this));
    } else {
      gChromeWin.Reader.removeArticleFromCache(this._article.url , function(success) {
        dump("Reader:Remove (in reader) success=" + success);

        gChromeWin.sendMessageToJava({
          gecko: {
            type: "Reader:Removed",
            url: this._article.url
          }
        });
      }.bind(this));
    }
  },

  _onList: function Reader_onList() {
    if (!this._article)
      return;

    gChromeWin.sendMessageToJava({
      gecko: {
        type: "Reader:GoToReadingList"
      }
    });
  },

  _onShare: function Reader_onShare() {
    if (!this._article)
      return;

    gChromeWin.sendMessageToJava({
      gecko: {
        type: "Reader:Share",
        url: this._article.url,
        title: this._article.title
      }
    });
  },

  _onMarginSizeChange: function Reader_onMarginSizeChange(operation) {
    if (operation == this._STEP_INCREMENT)
      this._setMarginSize(this._marginSize + 5);
    else
      this._setMarginSize(this._marginSize - 5);
  },

  _setMarginSize: function Reader_setMarginSize(newMarginSize) {
    if (this._marginSize === newMarginSize)
      return;

    this._marginSize = Math.max(5, Math.min(25, newMarginSize));
    document.body.style.marginLeft = this._marginSize + "%";
    document.body.style.marginRight = this._marginSize + "%";

    this._updateImageMargins();

    Services.prefs.setIntPref("reader.margin_size", this._marginSize);
  },

  _onFontSizeChange: function Reader_onFontSizeChange(operation) {
    if (operation == this._STEP_INCREMENT)
      this._setFontSize(this._fontSize + 1);
    else
      this._setFontSize(this._fontSize - 1);
  },

  _setFontSize: function Reader_setFontSize(newFontSize) {
    if (this._fontSize === newFontSize)
      return;

    let bodyClasses = document.body.classList;

    if (this._fontSize > 0)
      bodyClasses.remove("font-size" + this._fontSize);

    this._fontSize = Math.max(1, Math.min(7, newFontSize));
    bodyClasses.add("font-size" + this._fontSize);

    Services.prefs.setIntPref("reader.font_size", this._fontSize);
  },

  _setColorScheme: function Reader_setColorScheme(newColorScheme) {
    if (this._colorScheme === newColorScheme)
      return;

    let bodyClasses = document.body.classList;

    if (this._colorScheme)
      bodyClasses.remove(this._colorScheme);

    this._colorScheme = newColorScheme;
    bodyClasses.add(this._colorScheme);

    Services.prefs.setCharPref("reader.color_scheme", this._colorScheme);
  },

  _getToolbarVisibility: function Reader_getToolbarVisibility() {
    return !this._toolbarElement.classList.contains("toolbar-hidden");
  },

  _setToolbarVisibility: function Reader_setToolbarVisibility(visible) {
    if (history.state)
      history.back();

    if (!this._toolbarEnabled)
      return;

    if (this._getToolbarVisibility() === visible)
      return;

    this._toolbarElement.classList.toggle("toolbar-hidden");

    if (!visible && !this._hasUsedToolbar) {
      this._hasUsedToolbar = Services.prefs.getBoolPref("reader.has_used_toolbar");
      if (!this._hasUsedToolbar) {
        gChromeWin.NativeWindow.toast.show(gStrings.GetStringFromName("aboutReader.toolbarTip"), "short");

        Services.prefs.setBoolPref("reader.has_used_toolbar", true);
        this._hasUsedToolbar = true;
      }
    }
  },

  _toggleToolbarVisibility: function Reader_toggleToolbarVisibility(visible) {
    this._setToolbarVisibility(!this._getToolbarVisibility());
  },

  _loadFromURL: function Reader_loadFromURL(url) {
    this._showProgress();

    gChromeWin.Reader.parseDocumentFromURL(url, function(article) {
      if (article)
        this._showContent(article);
      else
        this._showError(gStrings.GetStringFromName("aboutReader.loadError"));
    }.bind(this));
  },

  _loadFromTab: function Reader_loadFromTab(tabId, url) {
    this._showProgress();

    gChromeWin.Reader.getArticleForTab(tabId, url, function(article) {
      if (article)
        this._showContent(article);
      else
        this._showError(gStrings.GetStringFromName("aboutReader.loadError"));
    }.bind(this));
  },

  _requestFavicon: function Reader_requestFavicon() {
    gChromeWin.sendMessageToJava({
      gecko: {
        type: "Reader:FaviconRequest",
        url: this._article.url
      }
    });
  },

  _loadFavicon: function Reader_loadFavicon(url, faviconUrl) {
    if (this._article.url !== url)
      return;

    let link = document.createElement('link');
    link.rel = 'shortcut icon';
    link.href = faviconUrl;

    document.getElementsByTagName('head')[0].appendChild(link);
  },

  _updateImageMargins: function Reader_updateImageMargins() {
    let windowWidth = window.innerWidth;
    let contentWidth = this._contentElement.offsetWidth;
    let maxWidthStyle = windowWidth + "px !important";

    let setImageMargins = function(img) {
      if (!img._originalWidth)
        img._originalWidth = img.offsetWidth;

      let imgWidth = img._originalWidth;

      // If the image is taking more than half of the screen, just make
      // it fill edge-to-edge.
      if (imgWidth < contentWidth && imgWidth > windowWidth * 0.55)
        imgWidth = windowWidth;

      let sideMargin = Math.max((contentWidth - windowWidth) / 2,
                                (contentWidth - imgWidth) / 2);

      let imageStyle = sideMargin + "px !important";
      let widthStyle = imgWidth + "px !important";

      let cssText = "max-width: " + maxWidthStyle + ";" +
                    "width: " + widthStyle + ";" +
                    "margin-left: " + imageStyle + ";" +
                    "margin-right: " + imageStyle + ";";

      img.style.cssText = cssText;
    }

    let imgs = document.querySelectorAll(this._BLOCK_IMAGES_SELECTOR);
    for (let i = imgs.length; --i >= 0;) {
      let img = imgs[i];

      if (img.width > 0) {
        setImageMargins(img);
      } else {
        img.onload = function() {
          setImageMargins(img);
        }
      }
    }
  },

  _showError: function Reader_showError(error) {
    this._headerElement.style.display = "none";
    this._contentElement.innerHTML = error;
    this._contentElement.style.display = "block";

    document.title = error;
  },

  _showContent: function Reader_showContent(article) {
    this._article = article;

    let domain = Services.io.newURI(article.url, null, null).host;
    this._domainElement.innerHTML = domain;

    this._creditsElement.innerHTML = article.byline;

    this._titleElement.innerHTML = article.title;
    document.title = article.title;

    this._headerElement.style.display = "block";

    this._contentElement.innerHTML = article.content;
    this._updateImageMargins();

    this._contentElement.style.display = "block";


    this._toolbarEnabled = true;
    this._setToolbarVisibility(true);

    this._requestFavicon();
  },

  _hideContent: function Reader_hideContent() {
    this._headerElement.style.display = "none";
    this._contentElement.style.display = "none";
  },

  _showProgress: function Reader_showProgress() {
    this._headerElement.style.display = "none";
    this._contentElement.innerHTML = gStrings.GetStringFromName("aboutReader.loading");
    this._contentElement.style.display = "block";
  },

  _decodeQueryString: function Reader_decodeQueryString(url) {
    let result = {};
    let query = url.split("?")[1];
    if (query) {
      let pairs = query.split("&");
      for (let i = 0; i < pairs.length; i++) {
        let [name, value] = pairs[i].split("=");
        result[name] = decodeURIComponent(value);
      }
    }

    return result;
  },

  _setupStepControl: function Reader_setupStepControl(id, name, callback) {
    let stepControl = document.getElementById(id);

    let title = document.createElement("h1");
    title.innerHTML = name;
    stepControl.appendChild(title);

    let plusButton = document.createElement("div");
    plusButton.className = "button plus-button";
    stepControl.appendChild(plusButton);

    let minusButton = document.createElement("div");
    minusButton.className = "button minus-button";
    stepControl.appendChild(minusButton);

    plusButton.addEventListener("click", function(aEvent) {
      aEvent.stopPropagation();
      callback(this._STEP_INCREMENT);
    }.bind(this), true);

    minusButton.addEventListener("click", function(aEvent) {
      aEvent.stopPropagation();
      callback(this._STEP_DECREMENT);
    }.bind(this), true);
  },

  _setupSegmentedButton: function Reader_setupSegmentedButton(id, options, initialValue, callback) {
    let segmentedButton = document.getElementById(id);

    for (let i = 0; i < options.length; i++) {
      let option = options[i];

      let item = document.createElement("li");
      let link = document.createElement("a");
      link.innerHTML = option.name;
      item.appendChild(link);

      segmentedButton.appendChild(item);

      link.addEventListener("click", function(aEvent) {
        aEvent.stopPropagation();

        let items = segmentedButton.children;
        for (let j = items.length - 1; j >= 0; j--) {
          items[j].classList.remove("selected");
        }

        item.classList.add("selected");
        callback(option.value);
      }.bind(this), true);

      if (option.value === initialValue)
        item.classList.add("selected");
    }
  },

  _setupButton: function Reader_setupButton(id, callback) {
    let button = document.getElementById(id);

    button.addEventListener("click", function(aEvent) {
      aEvent.stopPropagation();
      callback();
    }, true);
  },

  _setupAllDropdowns: function Reader_setupAllDropdowns() {
    let dropdowns = document.getElementsByClassName("dropdown");

    for (let i = dropdowns.length - 1; i >= 0; i--) {
      let dropdown = dropdowns[i];

      let dropdownToggle = dropdown.getElementsByClassName("dropdown-toggle")[0];
      let dropdownPopup = dropdown.getElementsByClassName("dropdown-popup")[0];

      if (!dropdownToggle || !dropdownPopup)
        continue;

      let dropdownArrow = document.createElement("div");
      dropdownArrow.className = "dropdown-arrow";
      dropdownPopup.appendChild(dropdownArrow);

      let updatePopupPosition = function() {
          let popupWidth = dropdownPopup.offsetWidth + 30;
          let arrowWidth = dropdownArrow.offsetWidth;
          let toggleWidth = dropdownToggle.offsetWidth;
          let toggleLeft = dropdownToggle.offsetLeft;

          let popupShift = (toggleWidth - popupWidth) / 2;
          let popupLeft = Math.max(0, Math.min(window.innerWidth - popupWidth, toggleLeft + popupShift));
          dropdownPopup.style.left = popupLeft + "px";

          let arrowShift = (toggleWidth - arrowWidth) / 2;
          let arrowLeft = toggleLeft - popupLeft + arrowShift;
          dropdownArrow.style.left = arrowLeft + "px";
      }

      window.addEventListener("resize", function(aEvent) {
        updatePopupPosition();
      }, true);

      dropdownToggle.addEventListener("click", function(aEvent) {
        aEvent.stopPropagation();

        let dropdownClasses = dropdown.classList;

        if (dropdownClasses.contains("open")) {
          history.back();
        } else {
          updatePopupPosition();
          if (!this._closeAllDropdowns())
            history.pushState({ dropdown: 1 }, document.title);

          dropdownClasses.add("open");
        }
      }.bind(this), true);
    }
  },

  _closeAllDropdowns : function Reader_closeAllDropdowns() {
    let dropdowns = document.querySelectorAll(".dropdown.open");
    for (let i = dropdowns.length - 1; i >= 0; i--) {
      dropdowns[i].classList.remove("open");
    }

    return (dropdowns.length > 0)
  }
}
