import re

def update_slides():
    filepath = 'docs/final-demo/slides.html'
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # We need to replace everything from <!-- SLIDE 8 down to <!-- #deck -->
    # Let's find the start of slide 8
    start_marker = "<!-- ═══════════════════════════════════════════════════════════\n     SLIDE 8"
    end_marker = "</div><!-- #deck -->"

    start_idx = content.find(start_marker)
    end_idx = content.find(end_marker)

    if start_idx == -1 or end_idx == -1:
        print("Could not find markers!")
        return

    # Prepare the new sections
    new_slides = """<!-- ═══════════════════════════════════════════════════════════
     SLIDE 8 — DEMO INTRO
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-8">
  <div class="slide-header">
    <span class="section-tag tag-demo">3 of 4 · Live Demo</span>
    <h2 class="slide-title">Live Demos &amp; <span>3D Enclosure</span></h2>
  </div>
  <div class="content">
    <div class="row" style="gap:32px;align-items:stretch">
      <div class="col" style="flex:1">
        <div class="card">
          <div class="card-title" style="color:var(--pink)">What We'll Show</div>
          <div style="display:flex;flex-direction:column;gap:12px">
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(192,68,255,.15);border:1px solid var(--purple);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--purple)">📦</div>
              <div><strong style="color:var(--text)">3D block enclosure</strong><br><span style="font-size:14px;color:#9fa8da">Mechanical alignment mapping our topologies.</span></div>
            </div>
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(0,170,255,.15);border:1px solid var(--blue);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--blue)">1</div>
              <div><strong style="color:var(--text)">Interactive Walkthrough</strong><br><span style="font-size:14px;color:#9fa8da">Flutter app tutorial mode.</span></div>
            </div>
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(255,122,0,.15);border:1px solid var(--orange);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--orange)">2</div>
              <div><strong style="color:var(--text)">Output Blocks</strong><br><span style="font-size:14px;color:#9fa8da">LED, Note, Music Sequence.</span></div>
            </div>
          </div>
        </div>
      </div>
      <div class="col" style="flex:1">
        <div class="card" style="height:100%">
          <div class="card-title" style="color:var(--pink)">...continued</div>
          <div style="display:flex;flex-direction:column;gap:12px">
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(0,221,106,.15);border:1px solid var(--green);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--green)">3</div>
              <div><strong style="color:var(--text)">Loop Sequence</strong><br><span style="font-size:14px;color:#9fa8da">Repeating sequences.</span></div>
            </div>
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(255,31,122,.15);border:1px solid var(--pink);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--pink)">4</div>
              <div><strong style="color:var(--text)">If Sequence</strong><br><span style="font-size:14px;color:#9fa8da">Handling conditional logic.</span></div>
            </div>
            <div style="display:flex;align-items:flex-start;gap:14px">
              <div style="min-width:28px;height:28px;border-radius:50%;background:rgba(255,214,0,.15);border:1px solid var(--yellow);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--yellow)">5</div>
              <div><strong style="color:var(--text)">15 Block System Stress Test</strong><br><span style="font-size:14px;color:#9fa8da">Proving total system capacity.</span></div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 9 — DEMO 1: WALKTHROUGH TUTORIAL
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-9">
  <div class="slide-header">
    <span class="section-tag tag-demo">3 of 4 · Live Demo</span>
    <h2 class="slide-title">Demo 1 — <span>Walkthrough Tutorial</span></h2>
  </div>
  <div class="content">
    <div class="card" style="border-color:var(--blue)">
      <div class="card-title" style="color:var(--blue)">The Scenario</div>
      <ul class="checklist">
        <li><strong>Introduce:</strong> Flutter App guided tutorial mode for an <code>If</code> sequence.</li>
        <li><strong>Show:</strong> App visually guides user to connect specific blocks. Real-time validation progresses tutorial forward via topology changes.</li>
        <li><strong>Result:</strong> Reliable dynamic synchronization between physical hardware and educational app.</li>
      </ul>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 10 — DEMO 2: OUTPUT BLOCKS
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-10">
  <div class="slide-header">
    <span class="section-tag tag-demo">3 of 4 · Live Demo</span>
    <h2 class="slide-title">Demo 2 — <span>All Output Block Functionalities</span></h2>
  </div>
  <div class="content">
    <div class="card" style="border-color:var(--pink)">
      <div class="card-title" style="color:var(--pink)">The Scenario</div>
      <ul class="checklist">
        <li><strong>Introduce:</strong> How users configure output features via TFT.</li>
        <li><strong>Show:</strong> Testing the LED Color Flash, Note, and Music Sequence output blocks individually.</li>
        <li><strong>Result:</strong> Correct I2C command dissemination from Brain to multiple output modules successfully driving hardware (WS2812, LM386).</li>
      </ul>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 11 — DEMO 3: LOOP SEQUENCE
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-11">
  <div class="slide-header">
    <span class="section-tag tag-demo">3 of 4 · Live Demo</span>
    <h2 class="slide-title">Demo 3 — <span>Loop Sequence</span></h2>
  </div>
  <div class="content">
    <div class="card" style="border-color:var(--green)">
      <div class="card-title" style="color:var(--green)">The Scenario</div>
      <ul class="checklist">
        <li><strong>Introduce:</strong> Validating loop control flow in execution.</li>
        <li><strong>Show:</strong> A program structured with <code>Loop { 3 times } -> Output -> End Loop</code>. Execution triggers 3 output cycles.</li>
        <li><strong>Result:</strong> Brain firmware correctly caches instruction pointers for looping sequences and bounds conditions.</li>
      </ul>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 12 — DEMO 4: IF SEQUENCE
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-12">
  <div class="slide-header">
    <span class="section-tag tag-demo">3 of 4 · Live Demo</span>
    <h2 class="slide-title">Demo 4 — <span>If Sequence</span></h2>
  </div>
  <div class="content">
    <div class="card" style="border-color:var(--purple)">
      <div class="card-title" style="color:var(--purple)">The Scenario</div>
      <ul class="checklist">
        <li><strong>Introduce:</strong> Validating conditional logic.</li>
        <li><strong>Show:</strong> A program structured with <code>If -> Button Press -> Then -> Output -> End If</code>. Execution halts on condition.</li>
        <li><strong>Result:</strong> Hardware user-input seamlessly interrupts and continues code execution.</li>
      </ul>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 13 — DEMO 5: 15-BLOCK FULL STRESS TEST
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-13">
  <div class="slide-header">
    <span class="section-tag tag-demo">3 of 4 · Live Demo</span>
    <h2 class="slide-title">Demo 5 — <span>15 Block Stress Test</span></h2>
  </div>
  <div class="content">
    <div class="card" style="border-color:var(--orange)">
      <div class="card-title" style="color:var(--orange)">The Scenario</div>
      <div style="font-family:'Consolas',monospace;font-size:20px;color:white;margin-bottom:12px">
        [Brain] -> [Loop] -> [Note] -> [LED Flash] -> [Endloop] -> [Delay] -> [Music Seq] -> [Music Seq] -> [If] -> [Button] -> [Then] -> [Note] -> [Note] -> [LED Flash] -> [End If]
      </div>
      <ul class="checklist">
        <li><strong>Introduce:</strong> Testing maximum design capacity (14 child blocks, 1 brain).</li>
        <li><strong>Show:</strong> Complex multi-branch logic executed across the fully saturated I2C bus.</li>
        <li><strong>Result:</strong> No crashes and robust performance across memory limitations, I2C routing, and visual interface representation.</li>
      </ul>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 14 — ENGINEERING SPECS
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-14">
  <div class="slide-header">
    <span class="section-tag tag-specs">4 of 4 · Engineering Specs</span>
    <h2 class="slide-title">Three <span>Engineering Specifications</span></h2>
  </div>
  <div class="content">
    <div class="card" style="margin-bottom:24px">
      <table class="spec-table">
        <thead>
          <tr>
            <th style="width:48px">#</th>
            <th>Quantity Measured</th>
            <th>Units</th>
            <th>Target</th>
            <th>Why it matters</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td class="num accent-pink">1</td>
            <td><strong>LED color select → preview latency</strong><br><span style="font-size:16px;color:var(--dim)">On LED block: TFT touch selection → LED changes</span></td>
            <td class="num">ms</td>
            <td class="accent-pink">Mean ≤ 50 ms</td>
            <td style="font-size:16px;color:#9fa8da">UX responsiveness</td>
          </tr>
          <tr>
            <td class="num accent-blue">2</td>
            <td><strong>Config-change-to-app latency</strong><br><span style="font-size:16px;color:var(--dim)">Physical block add/remove → UI shows topology</span></td>
            <td class="num">ms</td>
            <td class="accent-blue">Mean ≤ 2000 ms</td>
            <td style="font-size:16px;color:#9fa8da">Validates full pipeline</td>
          </tr>
          <tr>
            <td class="num accent-orange">3</td>
            <td><strong>I2C SCL/SDA rise time</strong><br><span style="font-size:16px;color:var(--dim)">30%-70% Vdd transition on SCL at Brain header</span></td>
            <td class="num">ns</td>
            <td class="accent-orange">≤ 1000 ns</td>
            <td style="font-size:16px;color:#9fa8da">Signal integrity</td>
          </tr>
        </tbody>
      </table>
    </div>
    <p style="font-size:18px;color:var(--muted);text-align:center;font-weight:600">Each spec: 10 test runs &nbsp;·&nbsp; mean / std dev / min / max &nbsp;·&nbsp; Live demonstrations on video</p>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 15 — SPEC 1 & 2 RECAP
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-15">
  <div class="slide-header">
    <span class="section-tag tag-specs">4 of 4 · Engineering Specs</span>
    <h2 class="slide-title">Specs Letency Recap — <span>Met with Margin</span></h2>
  </div>
  <div class="content">
    <div class="row">
      <div class="col">
        <div class="card" style="border-color:var(--pink)">
          <div class="card-title" style="color:var(--pink)">Spec 1: Output Latency</div>
          <p style="font-size:24px;color:#fff">Target: ≤ 50 ms</p>
          <p style="font-size:36px;color:var(--green);font-weight:bold;margin-top:10px">Mean: 13.1 ms</p>
        </div>
      </div>
      <div class="col">
        <div class="card" style="border-color:var(--blue)">
          <div class="card-title" style="color:var(--blue)">Spec 2: App Topology Sync Latency</div>
          <p style="font-size:24px;color:#fff">Target: ≤ 2000 ms</p>
          <p style="font-size:36px;color:var(--green);font-weight:bold;margin-top:10px">Mean: 49.6 ms</p>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 16 — SPEC 3 (15 BLOCKS)
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-16">
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
            <li><strong>Target:</strong> ≤ 1000 ns (I2C standard-mode at 100 kHz)</li>
            <li><strong>Measurement:</strong> Oscilloscope 10%–90% on SCL during scan.</li>
            <li><strong>Why:</strong> Evaluates bus capacitance under absolute maximum real-world loading condition.</li>
          </ul>
        </div>
      </div>
      <div class="col">
        <div class="card" style="border-color:var(--green)">
          <div class="card-title" style="color:var(--green)">Result</div>
          <p style="font-size:24px;color:#fff">Target: ≤ 1000 ns</p>
          <p style="font-size:36px;color:var(--green);font-weight:bold;margin-top:10px">Mean: 289 ns</p>
          <p style="font-size:20px;color:var(--dim);margin-top:5px">Min: 270ns · Max: 300ns</p>
          <div style="margin-top:14px;padding:10px 14px;background:#0d1a0d;border:1px solid rgba(76,175,80,.25);border-radius:8px;font-size:16px;color:#9fa8da">
            <strong style="color:var(--green)">Met: Yes</strong> (Margin maintained despite highest internal pull-up and trace resistance).
          </div>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════
     SLIDE 17 — CLOSING
════════════════════════════════════════════════════════════ -->
<div class="slide" id="slide-17">
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
      <p style="font-size:24px;color:var(--text);line-height:1.6;margin-bottom:12px">
        <strong>The core thesis, delivered:</strong> a physical arrangement of blocks <em>is</em> a program —
        validated, executed, and felt in the hands.
      </p>
      <p style="font-size:18px;color:var(--dim)">We welcome any feedback!</p>
    </div>
  </div>
</div>

"""

    final_content = content[:start_idx] + new_slides + "\n" + content[end_idx:]

    # Make sure we don't have broken script references, although we already patched JS
    # since we removed slide-11 SVG animation logic, slide11Step logic would error if slide 11 has no SVG,
    # but our slide 11 is 'Loop Sequence' which doesn't have SVG elements with .flow-step.
    # The JS queries .flow-step on slide 11, finds nothing, logic exits safely.
    
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(final_content)
    print("Rewritten successfully")

if __name__ == '__main__':
    update_slides()
