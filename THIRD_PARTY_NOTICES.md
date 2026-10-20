# Third-party notices and credits

Contrail contains no third-party source code. Its DSP is written from published research:

- **Tape hysteresis (Drive):** the Jiles-Atherton model as solved in real time by
  J. Chowdhury, "Real-time physical modelling for analog tape machines", Proc. DAFx 2019.
  Written from the paper's equations. No code from ChowTapeModel (GPL) is used.
- **Playback losses (Age):** spacing and gap loss from H. N. Bertram, *Theory of Magnetic
  Recording*, Cambridge University Press, 1994.
- **Filters:** R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients".

Built with JUCE 8 (https://juce.com) and tested with Catch2 (BSL-1.0).

Airwindows' MIT-licensed tape plugins were agreed as an allowed source (2026-10-20). None of
their code is used yet; if any is, its MIT licence and credit go here.
