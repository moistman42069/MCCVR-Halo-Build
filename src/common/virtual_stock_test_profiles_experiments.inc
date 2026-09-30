// Temporary MCCVR Hybrid final-composition A/B experiment pack.
// Temporary IDs are assigned 128+ in file order.
//
// Test order: AB0 -> AB1 -> AB0.
// Both profiles remain in the inverse-neck family so the patched lifecycle
// should retain one Q0 across F1 profile switches/recenters.

MCCVR_EXPERIMENT_PROFILE(
    "AB0_NK100_ForceStock_Control",
    "AB0 - NK100 ForceStock",
    MakeHybridInverseNeckY3ProfileSettings(
        1.00f, 0.100f, 0.040f, 0.000f))

MCCVR_EXPERIMENT_PROFILE(
    "AB1_NK100_Production_Seat464_474",
    "AB1 - NK100 Production",
    MakeHybridInverseNeckY3ProductionCandidateSettings(
        1.00f, 0.100f, 0.040f, 0.000f))
