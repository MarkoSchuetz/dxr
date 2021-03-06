function doSearch(id) {
  var f = document.getElementById(id);
  var treename = 'mozilla-central';
  var args = f.string.value.split(/ +/);
  var string = "";
  // Figure out the right path separator to use with virtroot
  sep = virtroot[virtroot.length - 1] === '/' ? '' : '/';
  var url = virtroot + sep + 'search.cgi?tree=' + treename;

  for (var i = 0; i < args.length; i++) {
    var arg = args[i];

    if (/^path:.+/(arg))
      url += "&path=" + encodeURIComponent(/^path:(.+)/.exec(arg).slice(1,2));
    else if (/^ext:.+/(arg))
      url += "&ext=" + encodeURIComponent(/^ext:(.+)/.exec(arg).slice(1,2));
    else if (/^type:.+/(arg))
      url += "&type=" + encodeURIComponent(/^type:(.+)/.exec(arg).slice(1,2));
    else if (/^member:.+/(arg))
      url += "&member=" + encodeURIComponent(/^member:(.+)/.exec(arg).slice(1,2));
    else if (/^derived:.+/(arg))
      url += "&derived=" + encodeURIComponent(/^derived:(.+)/.exec(arg).slice(1,2));
    else if (/^callers:.+/(arg))
      url += "&callers=" + encodeURIComponent(/^callers:(.+)/.exec(arg).slice(1,2));
    else if (/^macro:.+/(arg))
      url += "&macro=" + encodeURIComponent(/^macro:(.+)/.exec(arg).slice(1,2));
    else if (/^warnings:.*/(arg)) {
      var warnings = /^warnings:(.*)/.exec(arg).slice(1,2);
      
      // see if user did warnings:<nothing>, meaning "show all warnings"
      if (warnings == '') 
          warnings = '*';

      url += "&warnings=" + encodeURIComponent(warnings);
    } else {
      string += arg + " ";
      continue;
    }
  }

  if (string.length > 0) {
    string = string.substring(0, string.length-1);
    if (/^\/.+\/$/(string)) {
      string = string.substring(1, string.length-1);
      url += "&regexp=on";
    }
    url += "&string=" + encodeURIComponent(string);
  }

  url = url.replace(/\&/g, '&amp;');
  window.location = url;
  return false;
}
