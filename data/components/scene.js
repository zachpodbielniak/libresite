/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Still first: zero clip bytes before explicit intent, including low-data
 * clients. No autoplay race, black failure frame or hidden focus control. */
(function () {
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  document.querySelectorAll('.scene__film').forEach(function (video) {
    const button = video.parentElement.querySelector('.scene__play');
    const connection = navigator.connection;
    let wanted = false;
    function stop() {
      wanted = false; video.pause(); video.classList.remove('is-playing'); button.textContent = 'Play film';
    }
    function preferences() {
      if (reduced.matches || (connection && connection.saveData)) {
        stop(); video.removeAttribute('src'); video.load(); button.hidden = true;
      } else button.hidden = false;
    }
    button.addEventListener('click', function () {
      if (reduced.matches || (connection && connection.saveData)) return;
      if (wanted) { stop(); return; }
      wanted = true;
      if (!video.getAttribute('src')) video.src = matchMedia('(max-width: 767px)').matches && video.dataset.portrait ? video.dataset.portrait : video.dataset.src;
      video.muted = true;
      video.play().then(function () { if (!wanted) { video.pause(); return; } video.classList.add('is-playing'); button.textContent = 'Pause film'; }).catch(stop);
    });
    video.addEventListener('error', function () { stop(); button.hidden = true; });
    document.addEventListener('visibilitychange', function () { if (document.hidden) stop(); });
    if ('IntersectionObserver' in window) new IntersectionObserver(function (entries) { if (!entries[0].isIntersecting) stop(); }).observe(video);
    reduced.addEventListener('change', preferences);
    if (connection && connection.addEventListener) connection.addEventListener('change', preferences);
    preferences();
  });
}());
