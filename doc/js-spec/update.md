Update Web API for Soletta
==========================

Introduction
------------
This document presents a JavaScript API based on the [Soletta Update API](http://solettaproject.github.io/docs/c-api/group__Update.html).

The use case is to check, download, check download progress, and install Soletta updates.

Web IDL
-------

```javascript
// var update = require('soletta/update');

[NoInterfaceObject]
interface Update: {
  Promise<UpdateInfo> check();
  Promise<DOMString> fetch();
  Promise<void> install();
  long getProgress(UpdateOperationType type);
  boolean cancel(UpdateOperationType type);
};

dictionary UpdateInfo {
  DOMString version;
  unsigned long long size;
  boolean needed;
};

enum UpdateOperationType { "check", "fetch", "install" };
```

Cancellable operations are defined by the implementations. If an operation cannot be canceled, or canceling is not supported for it, ```cancel``` SHOULD return ```false```.

If an ongoing operation is repeated, e.g. a call to ```fetch()``` is invoked while a fetch is ongoing, the previous operation SHOULD be canceled and its Promise SHOULD reject with ```AbortError```. However, implementations MAY reuse partially downloaded files in the next invocations of ```fetch()```, even though previous fetch operations failed.

The ```install()``` method is considered a transaction. The underlying implementation SHOULD ensure that a failed ```install()``` is rolled back, i.e. it does not leave the system in an indeterministic state.

#### Example
```javascript
var update = require('soletta/update');

update.check()
.then(info => {
  console.log("Update available and needed: " + info.needed ? "yes" : "no");
  if (info.needed) {
    console.log("Update version: " + info.version);
    console.log("Update size (bytes): " + info.size);

    var fetchTimer = setInterval(function() {
      var progress = update.getProgress("fetch");
      if (progress >= 0)
        console.log("Update fetch progress: " + progress + " %");
    }, 1000);

    update.fetch()
    .then(() => {
      clearTimeout(fetchTimer);

      update.install().then(() => {
        console.log("Update installed.");
      }).catch((err) => {
        console.log("Update installation failed.");
      });
    })
    .catch((err) => {
      clearTimeout(fetchTimer);
      console.log("Fetching update failed.");
    });
  }
})
.catch((err) => {
  console.log("Checking update failed.");
});

```
