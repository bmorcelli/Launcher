const ready = (callback: () => void) => {
  if (document.readyState === "complete" || document.readyState === "interactive") {
    callback();
  } else {
    document.addEventListener("DOMContentLoaded", callback, { once: true });
  }
};

ready(() => {
  const prefersReducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  const yearTarget = document.querySelector<HTMLElement>("[data-year]");
  if (yearTarget) {
    yearTarget.textContent = new Date().getFullYear().toString();
  }

  const navToggle = document.querySelector<HTMLButtonElement>("[data-nav-toggle]");
  const navPanel = document.querySelector<HTMLElement>("[data-nav]");
  const navClose = document.querySelector<HTMLButtonElement>("[data-nav-close]");

  if (navToggle && navPanel) {
    const closeNav = () => {
      navPanel.classList.remove("is-open");
      navToggle.setAttribute("aria-expanded", "false");
    };

    const toggleNav = () => {
      const isOpen = navPanel.classList.toggle("is-open");
      navToggle.setAttribute("aria-expanded", isOpen.toString());
      if (isOpen) {
        navPanel.focus();
      }
    };

    navToggle.addEventListener("click", toggleNav);

    navClose?.addEventListener("click", () => {
      closeNav();
      navToggle.focus();
    });

    navPanel.addEventListener("keydown", (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        closeNav();
        navToggle.focus();
      }
    });

    document.addEventListener("click", (event: MouseEvent) => {
      if (!navPanel.classList.contains("is-open")) {
        return;
      }
      const target = event.target as Node;
      if (navPanel.contains(target) || navToggle.contains(target)) {
        return;
      }
      closeNav();
    });
  }

  const navLinks = Array.from(document.querySelectorAll<HTMLAnchorElement>("[data-scroll]"));
  if (navLinks.length > 0) {
    navLinks.forEach((link) => {
      link.addEventListener("click", (event) => {
        const targetId = link.getAttribute("href");
        if (targetId && targetId.startsWith("#")) {
          const section = document.querySelector<HTMLElement>(targetId);
          if (section) {
            event.preventDefault();
            if (navPanel) {
              navPanel.classList.remove("is-open");
              navToggle?.setAttribute("aria-expanded", "false");
            }
            section.scrollIntoView({ behavior: prefersReducedMotion ? "auto" : "smooth" });
            section.focus({ preventScroll: true });
          }
        }
      });
    });
  }

  const revealElements = Array.from(document.querySelectorAll<HTMLElement>(".reveal-on-scroll"));
  if (revealElements.length > 0 && "IntersectionObserver" in window) {
    const observer = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            entry.target.classList.add("is-visible");
          }
        });
      },
      { threshold: 0.1 }
    );

    revealElements.forEach((element) => observer.observe(element));
  } else {
    revealElements.forEach((element) => element.classList.add("is-visible"));
  }

  const filterInput = document.querySelector<HTMLInputElement>("[data-filter-input]");
  const firmwareItems = Array.from(document.querySelectorAll<HTMLElement>("[data-filter-item]"));
  const emptyState = document.querySelector<HTMLElement>("[data-filter-empty]");

  if (filterInput && firmwareItems.length > 0) {
    const normalize = (value: string) => value.trim().toLowerCase();

    const applyFilter = () => {
      const term = normalize(filterInput.value);
      let visibleCount = 0;

      firmwareItems.forEach((item) => {
        const haystack = normalize(item.dataset.filterValue ?? item.textContent ?? "");
        const matches = haystack.includes(term);
        item.toggleAttribute("hidden", !matches);
        if (matches) {
          visibleCount += 1;
        }
      });

      if (emptyState) {
        emptyState.toggleAttribute("hidden", visibleCount !== 0);
      }
    };

    filterInput.addEventListener("input", applyFilter);
    applyFilter();
  }
});
