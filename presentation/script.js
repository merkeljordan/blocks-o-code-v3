const slides       = document.querySelectorAll('.slide');
const prevBtn      = document.getElementById('prev-btn');
const nextBtn      = document.getElementById('next-btn');
const progress     = document.getElementById('progress');
const slideCounter = document.getElementById('slide-counter');
const notesOverlay = document.getElementById('notes-overlay');
const notesContent = notesOverlay.querySelector('.notes-content');

let currentSlide = 0;
let notesVisible = false;

function goTo(index) {
  if (index < 0 || index >= slides.length) return;
  currentSlide = index;
  updateSlides();
}

function updateSlides() {
  slides.forEach((slide, i) => {
    slide.classList.remove('active', 'previous');
    if (i === currentSlide) {
      // Force reflow so CSS animations restart on revisit
      slide.style.display = 'none';
      void slide.offsetHeight;
      slide.style.display = 'flex';
      slide.classList.add('active');
    } else if (i < currentSlide) {
      slide.classList.add('previous');
    }
  });

  const pct = ((currentSlide + 1) / slides.length) * 100;
  progress.style.width = `${pct}%`;
  slideCounter.textContent = `${currentSlide + 1} / ${slides.length}`;

  prevBtn.disabled = currentSlide === 0;
  nextBtn.disabled = currentSlide === slides.length - 1;

  if (notesVisible) refreshNotes();
}

function refreshNotes() {
  const notesEl = slides[currentSlide].querySelector('.notes');
  notesContent.innerHTML = notesEl
    ? notesEl.innerHTML
    : '<p>No notes for this slide.</p>';
}

nextBtn.addEventListener('click', () => goTo(currentSlide + 1));
prevBtn.addEventListener('click', () => goTo(currentSlide - 1));

document.addEventListener('keydown', (e) => {
  switch (e.key) {
    case 'ArrowRight':
    case ' ':
      e.preventDefault();
      goTo(currentSlide + 1);
      break;
    case 'ArrowLeft':
      goTo(currentSlide - 1);
      break;
    case 'ArrowDown':
      e.preventDefault();
      goTo(currentSlide + 1);
      break;
    case 'ArrowUp':
      goTo(currentSlide - 1);
      break;
    case 'Home':
      goTo(0);
      break;
    case 'End':
      goTo(slides.length - 1);
      break;
    case 'n':
    case 'N':
      notesVisible = !notesVisible;
      notesOverlay.style.display = notesVisible ? 'block' : 'none';
      if (notesVisible) refreshNotes();
      break;
  }
});

// Initialize
updateSlides();
