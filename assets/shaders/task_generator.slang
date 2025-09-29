struct DummyPayload {
    uint data;
};

[shader("amplification")]
[numthreads(1,1,1)]
void main()
{
    DummyPayload payload;
    payload.data = 0;
    DispatchMesh(1, 1, 1, payload);
}

