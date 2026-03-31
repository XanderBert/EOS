module dofCommon;

public float ComputeCoC(float dist, float focusDistance, float focusRange)
{
    float coc = abs(dist - focusDistance) / max(focusRange, 0.0001);
    return saturate(coc);
}
