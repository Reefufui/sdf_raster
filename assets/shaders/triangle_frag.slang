struct FragmentInput {
    float4 color : COLOR0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader ("fragment")]
FragmentOutput main(FragmentInput input) {
    FragmentOutput output;
    output.color = input.color;
    return output;
}
