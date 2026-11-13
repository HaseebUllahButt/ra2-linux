# Frame sampler diagnostics

Earlier Isle of War diagnostics used an in-process sampler to find when two gameplay traces first diverged. Two native runs each recorded all 9,000 frame boundaries, 300 shared census samples and 24,208 object rows. Their recorded damage events and census rows matched.

The presentation random state differed between the two repeat runs. It depends on when the display and sound paths draw random numbers. The later verification compares scenario random state and sampled houses and units. See the [skirmish](skirmish.md) and [release candidate](release.md) reports for those results.

The sampler observes selected damage writes and only records objects at the selected sample points. It cannot assign every hit to an attacker in an original capture. See the [sampler usage](../../sampler-tools/) for inputs and output.
