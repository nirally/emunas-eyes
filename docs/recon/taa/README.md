# TAA recon (Stage 2, 2026-10-14) — 1.0.3.47248, 2880x1620

- Catalyst's Video menu has no anti-aliasing option; TAA is always on (profile GstRender.AntiAliasingPost 3). Motion blur is a separate option (off).
- Frame trace (trace_1.txt, TRACE trigger): the game renders on the immediate context only. The TAA pass is one full-screen draw:
  in: t0 depth (R32G8X24), t1 motion vectors (RG16F), t2 R8 history A, t3 current colour, t4 colour history (RGBA16F), t5 R8 (static), t6 R8 history B
  out: r0 R8 history A', r1 scene colour (-> bloom, tonemap at the final composite), r2 colour history', r3 R8 (read by the composite), r4 R8 history B'
  The (t2,t4,t6) / (r0,r2,r4) sets ping-pong every frame: period 2 = the eye period, so each eye always read the other eye's history.
- Motion vectors: written by a full-screen camera pass (reads depth) + ~75 object draws. Stored as previous - current in clip space (NDC).
  VELOCITY probe, still scene, stereo: +-12 px (the eye separation as fake motion); mono ~0.
- Constants: per-view buffer (1136 bytes, 284 floats; viewcb_1.txt): 8..23 view, 24..39 projection (jitter at 26/30), 40..55 the frame's
  JITTERED view-projection, 72..87 the PREVIOUS frame's unjittered view-projection (D-034 - not this frame's), 56..71 / 88..103 the same without translation, 180..182 camera position,
  184..186 previous camera position. Most other constants go through a 1 MB ring (~305 Map(NO_OVERWRITE) writes per frame) bound with
  11.1 offsets; the motion-vector pass reads its previous camera from there, not from the per-view buffer.
- TAA jitter: pseudo-random sub-pixel offsets (+-0.5 px), well spread for each eye in alternate-eye rendering.
