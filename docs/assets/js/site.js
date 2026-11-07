(() => {
  "use strict";

  const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
  const progress = document.querySelector("[data-scroll-progress]");
  const tocLinks = document.querySelector("[data-toc-links]");
  let frameRequested = false;

  const updateScroll = () => {
    frameRequested = false;
    if (progress) {
      const distance = document.documentElement.scrollHeight - window.innerHeight;
      const fraction = distance > 0 ? Math.min(window.scrollY / distance, 1) : 0;
      progress.style.transform = `scaleX(${fraction})`;
    }
    if (tocLinks) {
      const headings = document.querySelectorAll(".doc-content h2[id]");
      let current = headings[0];
      headings.forEach((heading) => {
        if (heading.getBoundingClientRect().top < 150) current = heading;
      });
      tocLinks.querySelectorAll("a").forEach((link) => {
        link.classList.toggle("active", current && link.hash === `#${current.id}`);
      });
    }
  };

  const requestScrollUpdate = () => {
    if (!frameRequested) {
      frameRequested = true;
      window.requestAnimationFrame(updateScroll);
    }
  };

  window.addEventListener("scroll", requestScrollUpdate, { passive: true });
  window.addEventListener("resize", requestScrollUpdate, { passive: true });
  updateScroll();

  const reveals = document.querySelectorAll(".reveal");
  if (reducedMotion.matches || !("IntersectionObserver" in window)) {
    reveals.forEach((element) => element.classList.add("is-visible"));
  } else {
    const observer = new IntersectionObserver((entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add("is-visible");
          observer.unobserve(entry.target);
        }
      });
    }, { threshold: 0.08, rootMargin: "0px 0px -35px" });
    reveals.forEach((element) => observer.observe(element));
  }

  const counters = document.querySelectorAll("[data-count]");
  const showCount = (element) => {
    const target = Number(element.dataset.count);
    if (reducedMotion.matches) {
      element.textContent = String(target);
      return;
    }
    const start = performance.now();
    const tick = (now) => {
      const progress = Math.min((now - start) / 1200, 1);
      element.textContent = String(Math.round(target * (1 - Math.pow(1 - progress, 3))));
      if (progress < 1) window.requestAnimationFrame(tick);
    };
    window.requestAnimationFrame(tick);
  };

  if ("IntersectionObserver" in window && !reducedMotion.matches) {
    const counterObserver = new IntersectionObserver((entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          showCount(entry.target);
          counterObserver.unobserve(entry.target);
        }
      });
    }, { threshold: 0.5 });
    counters.forEach((counter) => counterObserver.observe(counter));
  } else {
    counters.forEach(showCount);
  }

  const tilt = document.querySelector("[data-tilt]");
  const tiltMedia = window.matchMedia("(min-width: 901px) and (pointer: fine)");
  if (tilt) {
    const resetTilt = () => {
      tilt.style.setProperty("--tilt-x", "2deg");
      tilt.style.setProperty("--tilt-y", "-4deg");
    };
    tilt.addEventListener("pointermove", (event) => {
      if (reducedMotion.matches || !tiltMedia.matches) return;
      const bounds = tilt.getBoundingClientRect();
      const x = (event.clientX - bounds.left) / bounds.width - 0.5;
      const y = (event.clientY - bounds.top) / bounds.height - 0.5;
      tilt.style.setProperty("--tilt-x", `${2 - y * 4}deg`);
      tilt.style.setProperty("--tilt-y", `${-4 + x * 5}deg`);
    });
    tilt.addEventListener("pointerleave", resetTilt);
    tiltMedia.addEventListener("change", resetTilt);
  }

  const copyText = async (text) => {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text);
      return;
    }
    const field = document.createElement("textarea");
    field.value = text;
    field.setAttribute("aria-hidden", "true");
    field.style.position = "fixed";
    field.style.opacity = "0";
    document.body.appendChild(field);
    field.select();
    const copied = document.execCommand("copy");
    field.remove();
    if (!copied) throw new Error("Copy unavailable");
  };

  document.querySelectorAll("[data-copy-target]").forEach((button) => {
    button.addEventListener("click", async () => {
      const source = document.getElementById(button.dataset.copyTarget);
      if (!source) return;
      try {
        await copyText(source.textContent.trim());
        button.textContent = "Copied";
        button.disabled = true;
        window.setTimeout(() => {
          button.textContent = "Copy";
          button.disabled = false;
        }, 1800);
      } catch {
        button.textContent = "Select text";
        const selection = window.getSelection();
        const range = document.createRange();
        range.selectNodeContents(source);
        selection.removeAllRanges();
        selection.addRange(range);
      }
    });
  });

  const article = document.querySelector(".doc-content");
  if (article) {
    article.querySelectorAll("table").forEach((table) => {
      const wrapper = document.createElement("div");
      wrapper.className = "table-scroll";
      wrapper.tabIndex = 0;
      wrapper.setAttribute("role", "region");
      wrapper.setAttribute("aria-label", "Scrollable table");
      table.before(wrapper);
      wrapper.appendChild(table);
    });

    const headings = article.querySelectorAll("h2[id], h3[id], h4[id]");
    headings.forEach((heading) => {
      const anchor = document.createElement("a");
      anchor.className = "heading-anchor";
      anchor.href = `#${heading.id}`;
      anchor.setAttribute("aria-label", `Link to ${heading.textContent}`);
      anchor.textContent = "#";
      heading.appendChild(anchor);
    });

    if (tocLinks) {
      article.querySelectorAll("h2[id]").forEach((heading) => {
        const label = heading.cloneNode(true);
        label.querySelectorAll(".heading-anchor").forEach((anchor) => anchor.remove());
        const link = document.createElement("a");
        link.href = `#${heading.id}`;
        link.textContent = label.textContent;
        tocLinks.appendChild(link);
      });
      const toc = document.querySelector("[data-toc]");
      if (toc && tocLinks.children.length) toc.hidden = false;
      updateScroll();
    }
  }

  const mobileNav = document.querySelector(".mobile-nav");
  if (mobileNav) {
    mobileNav.querySelectorAll("a").forEach((link) => {
      link.addEventListener("click", () => mobileNav.removeAttribute("open"));
    });
    document.addEventListener("click", (event) => {
      if (!mobileNav.contains(event.target)) mobileNav.removeAttribute("open");
    });
    mobileNav.addEventListener("keydown", (event) => {
      if (event.key === "Escape") {
        mobileNav.removeAttribute("open");
        mobileNav.querySelector("summary").focus();
      }
    });
  }
})();
