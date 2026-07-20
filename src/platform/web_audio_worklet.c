/*
 * web_audio_worklet.c — web-only AudioWorklet output backend (see the header).
 *
 * All logic lives in EM_JS bridges to Web Audio; on native this is an empty
 * translation unit. The engine's synth/mixer/mute/master-volume stages are
 * unchanged and upstream — this backend is a leaf swap at the SDL_QueueAudio
 * boundary (osAiSetNextBuffer in stubs.c), web-only, behind __EMSCRIPTEN__.
 */
/* Non-empty on native (this whole file is web-only); avoids an
 * empty-translation-unit warning under -Werror on the native build. */
typedef int web_audio_worklet_tu_guard;

#ifdef __EMSCRIPTEN__

#include "web_audio_worklet.h"

#include <emscripten/em_js.h>

/*
 * The worklet processor source, injected as a Blob module so no 7th deploy file
 * is needed (preserves the docs/WEB.md 6-file invariant). It:
 *   - keeps a 1-second ring buffer per channel;
 *   - on a 'pcm' message, converts interleaved s16 -> float and fills the ring;
 *   - in process(), emits one render quantum, resampling srcRate -> the context
 *     rate with a fractional read pointer + linear interpolation (ratio == 1,
 *     i.e. a plain copy, when the browser honours the requested 22050 context);
 *   - reports its live ring count back every 3 quanta so the main thread can
 *     estimate occupancy without shared memory.
 */
EM_JS(int, webAudioOutputInit, (int srcRate, int channels), {
    try {
        var AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) return 0;
        var ctx;
        try { ctx = new AC({ sampleRate: srcRate, latencyHint: 'interactive' }); }
        catch (e) { ctx = new AC({ latencyHint: 'interactive' }); }
        if (!ctx || !ctx.audioWorklet) { try { if (ctx) ctx.close(); } catch (e2) {} return 0; }

        var A = {
            ctx: ctx, node: null, ready: false, failed: false, pending: [],
            posted: 0, statRing: 0, statTime: 0, postedAtStat: 0, under: 0,
            srcRate: srcRate, chans: channels
        };
        Module.mgb64Audio = A;
        /* Reuse the field the shell's WEB-035/WEB-009 resume+close lifecycle
         * already manages, so gesture-resume and teardown work unchanged. Safe
         * because we skip SDL_OpenAudioDevice on web, so SDL never sets it. */
        Module.SDL2 = Module.SDL2 || {};
        Module.SDL2.audioContext = ctx;

        var src =
            "class MGB64Ring extends AudioWorkletProcessor{" +
              "constructor(o){super();" +
                "var p=(o&&o.processorOptions)||{};" +
                "this.cap=(p.srcRate||22050)|0;" +               /* 1 s ring */
                "this.ratio=(p.srcRate||22050)/sampleRate;" +    /* src frames per out frame */
                "this.L=new Float32Array(this.cap);this.R=new Float32Array(this.cap);" +
                "this.r=0;this.w=0;this.count=0;this.frac=0;this.under=0;this.q=0;" +
                "this.port.onmessage=(e)=>{var d=e.data;if(d&&d.cmd==='pcm'){" +
                  "var s=new Int16Array(d.buf);var n=s.length>>1;" +
                  "for(var i=0;i<n;i++){if(this.count>=this.cap)break;" +
                    "this.L[this.w]=s[2*i]/32768;this.R[this.w]=s[2*i+1]/32768;" +
                    "this.w=(this.w+1)%this.cap;this.count++;}}};}" +
              "process(inputs,outputs){var o=outputs[0];if(!o||!o[0])return true;" +
                "var L=o[0];var R=o[1]||o[0];var n=L.length;" +
                "for(var i=0;i<n;i++){" +
                  "if(this.count<2){L[i]=0;R[i]=0;this.under++;continue;}" +
                  "var i1=(this.r+1)%this.cap;var f=this.frac;" +
                  "L[i]=this.L[this.r]*(1-f)+this.L[i1]*f;" +
                  "R[i]=this.R[this.r]*(1-f)+this.R[i1]*f;" +
                  "this.frac+=this.ratio;" +
                  "while(this.frac>=1){this.frac-=1;this.r=(this.r+1)%this.cap;this.count--;}}" +
                "if(++this.q>=3){this.q=0;this.port.postMessage({ring:this.count,under:this.under});}" +
                "return true;}}" +
            "registerProcessor('mgb64-ring',MGB64Ring);";

        var url = URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
        ctx.audioWorklet.addModule(url).then(function () {
            try { URL.revokeObjectURL(url); } catch (e) {}
            var node = new AudioWorkletNode(ctx, 'mgb64-ring', {
                numberOfInputs: 0, numberOfOutputs: 1,
                outputChannelCount: [channels],
                processorOptions: { srcRate: srcRate }
            });
            node.port.onmessage = function (e) {
                A.statRing = e.data.ring;
                A.statTime = ctx.currentTime;
                A.postedAtStat = A.posted;
                A.under = e.data.under;
            };
            node.connect(ctx.destination);
            A.node = node;
            A.ready = true;
            for (var i = 0; i < A.pending.length; i++) {
                node.port.postMessage({ cmd: 'pcm', buf: A.pending[i] }, [A.pending[i]]);
            }
            A.pending = [];
        }).catch(function (err) {
            /* Module load failed after we committed to this backend; mark failed
             * so ai_enqueue/occupancy fall back to reporting nothing. Audio is
             * lost in this rare case, but the game continues. */
            A.failed = true;
            if (console && console.error) console.error('[audio] AudioWorklet module load failed: ' + err);
        });
        return 1;
    } catch (e) {
        return 0;
    }
});

EM_JS(int, webAudioOutputActive, (void), {
    return (Module.mgb64Audio && !Module.mgb64Audio.failed) ? 1 : 0;
});

EM_JS(int, webAudioOutputPush, (const void *buf, unsigned size), {
    var A = Module.mgb64Audio;
    if (!A || A.failed) return -1;
    var frames = size >> 2;                       /* stereo s16 -> 4 bytes/frame */
    /* Copy out of the (growable) wasm heap into an owned, transferable buffer. */
    var view = HEAP16.subarray(buf >> 1, (buf >> 1) + (size >> 1));
    var copy = new Int16Array(view);
    if (A.ready && A.node) {
        A.node.port.postMessage({ cmd: 'pcm', buf: copy.buffer }, [copy.buffer]);
    } else if (A.pending.length < 64) {           /* buffer briefly until module loads */
        A.pending.push(copy.buffer);
    }
    A.posted += frames;
    return 0;
});

EM_JS(unsigned, webAudioOutputQueuedBytes, (void), {
    var A = Module.mgb64Audio;
    if (!A) return 0;
    var occ;
    if (A.statTime > 0) {
        var elapsed = A.ctx.currentTime - A.statTime;
        /* ring at last report, plus what we posted since, minus what has played */
        occ = A.statRing + (A.posted - A.postedAtStat) - elapsed * A.srcRate;
    } else {
        occ = A.posted;                           /* nothing has played yet */
    }
    if (occ < 0) occ = 0;
    return (occ | 0) * 4;
});

EM_JS(void, webAudioOutputResume, (void), {
    var A = Module.mgb64Audio;
    if (A && A.ctx && A.ctx.state !== 'running') {
        A.ctx.resume().catch(function () {});
    }
});

#endif /* __EMSCRIPTEN__ */
