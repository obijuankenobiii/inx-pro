let cards = [];

function escapeHtml(value) {
  return String(value || "").replace(/[&<>\"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;" })[c]);
}

async function loadCards() {
  const list = document.getElementById("card-list");
  try {
    const response = await fetch("/api/plugins/study/cards");
    const data = await response.json();
    if (!response.ok || !data.ok) throw new Error(data.error || "Could not load cards");
    cards = data.cards || [];
    document.getElementById("summary").textContent = `${cards.length} card${cards.length === 1 ? "" : "s"}`;
    list.innerHTML = cards.length ? cards.map((card) => `
      <article class="card-row">
        <div class="card-front">${escapeHtml(card.front)}</div>
        <div class="card-back">${escapeHtml(card.back || "No answer yet")}</div>
        <div class="card-meta">${escapeHtml([card.book, card.chapter, card.tags].filter(Boolean).join(" · "))}</div>
      </article>`).join("") : '<div class="empty">No study cards yet. Select text in a book and choose “Add to study”.</div>';
  } catch (error) {
    document.getElementById("summary").textContent = "Unavailable";
    list.innerHTML = `<div class="empty">${escapeHtml(error.message)}</div>`;
  }
}

function downloadCards(format) {
  window.location.href = `/api/plugins/study/export?format=${encodeURIComponent(format)}`;
}

loadCards();
