import re

def rewrite():
    filepath = 'docs/final-demo/slides.html'
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Part 1: Rewrite Slide 8 (remove 3D enclosure)
    # Finding Slide 8 marker
    s8_start = content.find('<!-- ═══════════════════════════════════════════════════════════\n     SLIDE 8 — DEMO INTRO')
    s9_start = content.find('<!-- ═══════════════════════════════════════════════════════════\n     SLIDE 9 — DEMO 1: WALKTHROUGH TUTORIAL')
    
    if s8_start == -1 or s9_start == -1:
        print("Could not find Slide 8 or Slide 9 marker.")
        return
        
    s8_new = """<!-- ═══════════════════════════════════════════════════════════
     SLIDE 8 — DEMO INTRO
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-8">
  <div class="slide-header">
    <span class="section-tag tag-demo">3 of 4 · Live Demo</span>
    <h2 class="slide-title">Live Demos &amp; <span>Scenarios</span></h2>
  </div>
  <div class="content">
    <div class="row" style="gap:32px;align-items:stretch">
      <div class="col" style="flex:1">
        <div class="card">
          <div class="card-title" style="color:var(--pink)">What We'll Show</div>
          <div style="display:flex;flex-direction:column;gap:12px">
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(0,170,255,.15);border:1px solid var(--blue);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--blue)">1</div>
              <div><strong style="color:var(--text)">Interactive Walkthrough</strong><br><span style="font-size:24px;color:#9fa8da">Flutter app tutorial mode.</span></div>
            </div>
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(255,122,0,.15);border:1px solid var(--orange);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--orange)">2</div>
              <div><strong style="color:var(--text)">Output Blocks</strong><br><span style="font-size:24px;color:#9fa8da">LED, Note, Music Sequence.</span></div>
            </div>
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(0,221,106,.15);border:1px solid var(--green);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--green)">3</div>
              <div><strong style="color:var(--text)">Loop Sequence</strong><br><span style="font-size:24px;color:#9fa8da">Repeating sequences.</span></div>
            </div>
          </div>
        </div>
      </div>
      <div class="col" style="flex:1">
        <div class="card" style="height:100%">
          <div class="card-title" style="color:var(--pink)">...continued</div>
          <div style="display:flex;flex-direction:column;gap:12px">
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(255,31,122,.15);border:1px solid var(--pink);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--pink)">4</div>
              <div><strong style="color:var(--text)">If Sequence</strong><br><span style="font-size:24px;color:#9fa8da">Handling conditional logic.</span></div>
            </div>
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(255,214,0,.15);border:1px solid var(--yellow);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--yellow)">5</div>
              <div><strong style="color:var(--text)">15 Block System Stress Test</strong><br><span style="font-size:24px;color:#9fa8da">Proving total system capacity.</span></div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>

"""
    content = content[:s8_start] + s8_new + content[s9_start:]
    
    # Part 2: Rewrite Slides 15, 16, 17 into 15 (Spec 1), 16 (Spec 2), 17 (Spec 3), 18 (Closing)
    s15_start = content.find('<!-- ═══════════════════════════════════════════════════════════\n     SLIDE 15 — SPEC 1 & 2 RECAP')
    deck_end = content.find('</div><!-- #deck -->')
    
    specs_new = """<!-- ═══════════════════════════════════════════════════════════
     SLIDE 15 — SPEC 1
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-15">
  <div class="slide-header">
    <span class="section-tag tag-specs">4 of 4 · Engineering Specs</span>
    <h2 class="slide-title">Spec 1 — <span>LED Color Preview Latency</span></h2>
  </div>
  <div class="content">
    <div class="row">
      <div class="col">
        <div class="card" style="border-color:var(--pink)">
          <div class="card-title" style="color:var(--pink)">Definition</div>
          <ul class="checklist" style="gap:8px">
            <li><strong>Quantity:</strong> Time from TFT touch color selection → LED matrix color change</li>
            <li><strong>Units:</strong> milliseconds (ms)</li>
            <li><strong>Target:</strong> Mean ≤ 50 ms</li>
          </ul>
        </div>
        <div class="card" style="border-color:var(--border)">
          <div class="card-title" style="color:var(--muted)">Demo Video Placeholder</div>
          <div style="width:100%;height:300px;background:#1a1a2e;border:1px dashed #4a5490;display:flex;align-items:center;justify-content:center;border-radius:12px">
             <span style="color:#8b9de8;font-size:32px;font-weight:bold;">[ INSERT VIDEO HERE ]</span>
          </div>
        </div>
      </div>
      <div class="col">
        <div class="card">
          <div class="card-title" style="color:var(--pink)">10-Run Results</div>
          <table class="spec-table">
            <thead><tr><th>Run</th><th>Latency (ms)</th><th>Run</th><th>Latency (ms)</th></tr></thead>
            <tbody>
              <tr><td class="num">1</td><td class="num" style="color:var(--dim)">13.206</td><td class="num">6</td><td class="num" style="color:var(--dim)">13.129</td></tr>
              <tr><td class="num">2</td><td class="num" style="color:var(--dim)">13.131</td><td class="num">7</td><td class="num" style="color:var(--dim)">13.133</td></tr>
              <tr><td class="num">3</td><td class="num" style="color:var(--dim)">13.142</td><td class="num">8</td><td class="num" style="color:var(--dim)">13.138</td></tr>
              <tr><td class="num">4</td><td class="num" style="color:var(--dim)">13.133</td><td class="num">9</td><td class="num" style="color:var(--dim)">13.133</td></tr>
              <tr><td class="num">5</td><td class="num" style="color:var(--dim)">13.133</td><td class="num">10</td><td class="num" style="color:var(--dim)">13.128</td></tr>
            </tbody>
          </table>
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:16px">
            <div style="background:#14142a;border-radius:8px;padding:12px;text-align:center">
              <div style="font-size:20px;color:var(--dim);margin-bottom:4px">MEAN</div>
              <div style="font-size:36px;font-weight:700;color:var(--pink)">13.141 ms</div>
            </div>
            <div style="background:#14142a;border-radius:8px;padding:12px;text-align:center">
              <div style="font-size:20px;color:var(--dim);margin-bottom:4px">MAX</div>
              <div style="font-size:36px;font-weight:700;color:var(--orange)">13.206 ms</div>
            </div>
          </div>
          <div style="margin-top:14px;padding:12px 14px;background:#0d1a0d;border:1px solid rgba(76,175,80,.25);border-radius:8px;font-size:24px;color:#9fa8da">
            Target: Mean ≤ 50 ms &nbsp;·&nbsp; <strong style="color:var(--green)">Met: Yes</strong>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 16 — SPEC 2
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-16">
  <div class="slide-header">
    <span class="section-tag tag-specs">4 of 4 · Engineering Specs</span>
    <h2 class="slide-title">Spec 2 — <span>Config-Change-to-App Latency</span></h2>
  </div>
  <div class="content">
    <div class="row">
      <div class="col">
        <div class="card" style="border-color:var(--blue)">
          <div class="card-title" style="color:var(--blue)">Definition</div>
          <ul class="checklist" style="gap:8px">
            <li><strong>Quantity:</strong> Physical block add/remove → app UI reflects new topology</li>
            <li><strong>Units:</strong> milliseconds (ms)</li>
            <li><strong>Target:</strong> Mean ≤ 2000 ms</li>
          </ul>
        </div>
        <div class="card" style="border-color:var(--border)">
          <div class="card-title" style="color:var(--muted)">Demo Video Placeholder</div>
          <div style="width:100%;height:300px;background:#1a1a2e;border:1px dashed #4a5490;display:flex;align-items:center;justify-content:center;border-radius:12px">
             <span style="color:#8b9de8;font-size:32px;font-weight:bold;">[ INSERT VIDEO HERE ]</span>
          </div>
        </div>
      </div>
      <div class="col">
        <div class="card">
          <div class="card-title" style="color:var(--blue)">10-Run Results</div>
          <table class="spec-table">
            <thead><tr><th>Run</th><th>Latency (ms)</th><th>Run</th><th>Latency (ms)</th></tr></thead>
            <tbody>
              <tr><td class="num">1</td><td class="num" style="color:var(--dim)">45</td><td class="num">6</td><td class="num" style="color:var(--dim)">62</td></tr>
              <tr><td class="num">2</td><td class="num" style="color:var(--dim)">60</td><td class="num">7</td><td class="num" style="color:var(--dim)">63</td></tr>
              <tr><td class="num">3</td><td class="num" style="color:var(--dim)">39</td><td class="num">8</td><td class="num" style="color:var(--dim)">70</td></tr>
              <tr><td class="num">4</td><td class="num" style="color:var(--dim)">46</td><td class="num">9</td><td class="num" style="color:var(--dim)">33</td></tr>
              <tr><td class="num">5</td><td class="num" style="color:var(--dim)">37</td><td class="num">10</td><td class="num" style="color:var(--dim)">41</td></tr>
            </tbody>
          </table>
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:16px">
            <div style="background:#14142a;border-radius:8px;padding:12px;text-align:center">
              <div style="font-size:20px;color:var(--dim);margin-bottom:4px">MEAN</div>
              <div style="font-size:36px;font-weight:700;color:var(--blue)">49.6 ms</div>
            </div>
            <div style="background:#14142a;border-radius:8px;padding:12px;text-align:center">
              <div style="font-size:20px;color:var(--dim);margin-bottom:4px">MAX</div>
              <div style="font-size:36px;font-weight:700;color:var(--orange)">70 ms</div>
            </div>
          </div>
          <div style="margin-top:14px;padding:12px 14px;background:#0d1a0d;border:1px solid rgba(76,175,80,.25);border-radius:8px;font-size:24px;color:#9fa8da">
            Target: Mean ≤ 2000 ms &nbsp;·&nbsp; <strong style="color:var(--green)">Met: Yes</strong>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 17 — SPEC 3
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-17">
  <div class="slide-header">
    <span class="section-tag tag-specs">4 of 4 · Engineering Specs</span>
    <h2 class="slide-title">Spec 3 — <span>I2C Rise Time (15-Block Max Load)</span></h2>
  </div>
  <div class="content">
    <div class="row">
      <div class="col">
        <div class="card" style="border-color:var(--orange)">
          <div class="card-title" style="color:var(--orange)">Definition</div>
          <ul class="checklist" style="gap:8px">
            <li><strong>Quantity Sequence:</strong> ALL 15 blocks attached to common I2C bus line.</li>
            <li><strong>Target:</strong> ≤ 1000 ns (I2C standard-mode)</li>
            <li><strong>Measurement:</strong> Oscilloscope 10%–90% on SCL.</li>
          </ul>
        </div>
        <div class="card" style="border-color:var(--border)">
          <div class="card-title" style="color:var(--muted)">Demo Video Placeholder</div>
          <div style="width:100%;height:300px;background:#1a1a2e;border:1px dashed #4a5490;display:flex;align-items:center;justify-content:center;border-radius:12px">
             <span style="color:#8b9de8;font-size:32px;font-weight:bold;">[ INSERT VIDEO HERE ]</span>
          </div>
        </div>
      </div>
      <div class="col">
        <div class="card">
          <div class="card-title" style="color:var(--orange)">10-Run Results</div>
          <table class="spec-table">
            <thead><tr><th>Run</th><th>Rise time (ns)</th><th>Run</th><th>Rise time (ns)</th></tr></thead>
            <tbody>
              <tr><td class="num">1</td><td class="num" style="color:var(--dim)">280</td><td class="num">6</td><td class="num" style="color:var(--dim)">290</td></tr>
              <tr><td class="num">2</td><td class="num" style="color:var(--dim)">300</td><td class="num">7</td><td class="num" style="color:var(--dim)">300</td></tr>
              <tr><td class="num">3</td><td class="num" style="color:var(--dim)">270</td><td class="num">8</td><td class="num" style="color:var(--dim)">290</td></tr>
              <tr><td class="num">4</td><td class="num" style="color:var(--dim)">270</td><td class="num">9</td><td class="num" style="color:var(--dim)">300</td></tr>
              <tr><td class="num">5</td><td class="num" style="color:var(--dim)">300</td><td class="num">10</td><td class="num" style="color:var(--dim)">290</td></tr>
            </tbody>
          </table>
          <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:16px">
            <div style="background:#14142a;border-radius:8px;padding:12px;text-align:center">
              <div style="font-size:20px;color:var(--dim);margin-bottom:4px">MEAN</div>
              <div style="font-size:36px;font-weight:700;color:var(--orange)">289 ns</div>
            </div>
            <div style="background:#14142a;border-radius:8px;padding:12px;text-align:center">
              <div style="font-size:20px;color:var(--dim);margin-bottom:4px">MAX</div>
              <div style="font-size:36px;font-weight:700;color:var(--red)">300 ns</div>
            </div>
          </div>
          <div style="margin-top:14px;padding:12px 14px;background:#0d1a0d;border:1px solid rgba(76,175,80,.25);border-radius:8px;font-size:24px;color:#9fa8da">
            Target: ≤ 1000 ns &nbsp;·&nbsp; <strong style="color:var(--green)">Met: Yes</strong>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 18 — CLOSING
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-18">
  <div class="slide-header">
    <span class="section-tag tag-close">Closing</span>
    <h2 class="slide-title">What We Built &amp; <span>What's Next</span></h2>
  </div>
  <div class="content">
    <div class="row" style="margin-bottom:24px">
      <div class="col">
        <div class="card" style="border-color:var(--green)">
          <div class="card-title" style="color:var(--green)">The Delivery</div>
          <ul class="checklist">
            <li>15 functional identical hardware modules behaving dynamically.</li>
            <li>Flutter companion app successfully modeling sequence state.</li>
            <li>Safe, modular execution environment allowing abstract logic building.</li>
            <li>Full adherence to strict standard electrical timing budgets.</li>
          </ul>
        </div>
      </div>
      <div class="col">
        <div class="card" style="border-color:var(--purple)">
          <div class="card-title" style="color:var(--purple)">What's Next</div>
          <ul class="checklist">
            <li>Refine PCB manufacturing constraints for mass array.</li>
            <li>Polish internal mechanics.</li>
            <li>Integrate app tutorials further.</li>
          </ul>
        </div>
      </div>
    </div>
    <div style="padding:24px 36px;background:linear-gradient(90deg,rgba(255,31,122,.12),rgba(192,68,255,.10),rgba(0,170,255,.09));border:1px solid rgba(255,31,122,.35);border-radius:14px;text-align:center;box-shadow:0 0 40px rgba(192,68,255,.12)">
      <p style="font-size:36px;color:var(--text);line-height:1.6;margin-bottom:12px">
        <strong>The core thesis, delivered:</strong> a physical arrangement of blocks <em>is</em> a program —
        validated, executed, and felt in the hands.
      </p>
      <p style="font-size:24px;color:var(--dim)">We welcome any feedback!</p>
    </div>
  </div>
</div>
"""
    content = content[:s15_start] + specs_new + "\n" + content[deck_end:]
    
    # Update TOTAL to 18
    content = content.replace("const TOTAL = 17;", "const TOTAL = 18;")
    
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)

if __name__ == '__main__':
    rewrite()
