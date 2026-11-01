// PLEASE DISCUSS — optional progressive enhancement. Every feature works
// without this file; it only adds conveniences.
(function () {
  "use strict";

  // Forms with data-confirm (Delete) ask before submitting.
  document.addEventListener("submit", function (e) {
    var form = e.target;
    var msg = form.getAttribute && form.getAttribute("data-confirm");
    if (msg && !window.confirm(msg)) e.preventDefault();
  });

  // Ctrl/Cmd+Enter in a textarea submits its form with the default button
  // (Post / Create / Save), never Preview.
  document.addEventListener("keydown", function (e) {
    var t = e.target;
    if (e.key !== "Enter" || !(e.ctrlKey || e.metaKey) || t.tagName !== "TEXTAREA" || !t.form) return;
    var go = t.form.querySelector("button[type=submit]:not([name=preview])");
    if (!go) return;
    e.preventDefault();
    if (t.form.requestSubmit) t.form.requestSubmit(go); else go.click();
  });
})();
