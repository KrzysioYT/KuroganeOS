"use strict";

(async () => {
  const panel = document.querySelector("[data-download-panel]");
  if (!panel) return;

  const status = panel.querySelector("[data-release-status]");
  const name = panel.querySelector("[data-release-name]");
  const meta = panel.querySelector("[data-release-meta]");
  const links = panel.querySelector("[data-release-links]");
  const sha = panel.querySelector("[data-release-sha]");

  try {
    const response = await fetch("https://downloads.kuroganeos.dev/latest.json", {
      cache: "no-store",
      credentials: "omit"
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const release = await response.json();
    if (!Array.isArray(release.files) || release.files.length === 0) {
      throw new Error("release manifest has no files");
    }

    status.textContent = "NAJNOWSZY OBRAZ GOTOWY";
    name.textContent = release.name || "KuroganeOS Development Preview";
    meta.textContent = [release.version, release.architecture, release.channel]
      .filter(Boolean).join(" · ");
    links.replaceChildren();

    for (const file of release.files) {
      const anchor = document.createElement("a");
      anchor.className = `button${file.kind === "iso" ? " primary" : ""}`;
      anchor.href = new URL(file.url, "https://downloads.kuroganeos.dev/").href;
      anchor.textContent = `Pobierz ${String(file.kind || "plik").toUpperCase()}`;
      anchor.rel = "noopener";
      links.append(anchor);
    }

    const first = release.files[0];
    sha.textContent = first.sha256 ? `SHA-256 ${first.sha256}` : "Sprawdź SHA256SUMS.txt";
    panel.classList.add("release-ready");
  } catch (error) {
    status.textContent = "OBRAZY DOSTĘPNE W CI";
    meta.textContent = "Publikacja na VPS oczekuje na najnowszy artefakt";
    sha.textContent = "Każdy obraz posiada SHA256SUMS.txt";
    panel.classList.add("release-fallback");
  }
})();
