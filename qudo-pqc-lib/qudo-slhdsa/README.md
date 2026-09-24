# QUDO SLH-DSA Library

Production-grade SLH-DSA stateless hash-based signatures (NIST FIPS 205).
Part of [QUDO PQC](../README.md).

## Parameter Sets

| Parameter Set | Security Level | Signature | Public Key | Secret Key |
|--------------|---------------|-----------|------------|------------|
| SLH-DSA-SHA2-128s | 1 | 7,856 B | 32 B | 64 B |
| SLH-DSA-SHA2-128f | 1 | 17,088 B | 32 B | 64 B |
| SLH-DSA-SHA2-192s | 3 | 16,224 B | 48 B | 96 B |
| SLH-DSA-SHA2-192f | 3 | 35,664 B | 48 B | 96 B |
| SLH-DSA-SHA2-256s | 5 | 29,792 B | 64 B | 128 B |
| SLH-DSA-SHA2-256f | 5 | 49,856 B | 64 B | 128 B |
| SLH-DSA-SHAKE-128s | 1 | 7,856 B | 32 B | 64 B |
| SLH-DSA-SHAKE-128f | 1 | 17,088 B | 32 B | 64 B |
| SLH-DSA-SHAKE-192s | 3 | 16,224 B | 48 B | 96 B |
| SLH-DSA-SHAKE-192f | 3 | 35,664 B | 48 B | 96 B |
| SLH-DSA-SHAKE-256s | 5 | 29,792 B | 64 B | 128 B |
| SLH-DSA-SHAKE-256f | 5 | 49,856 B | 64 B | 128 B |

"small" (s) = smaller signatures, slower. "fast" (f) = faster, larger signatures.

## Standalone Build

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

## API Usage

**Object API:**
```c
QUDO_SLHDSA *ctx = QUDO_SLHDSA_new(QUDO_SLHDSA_SHA2_128s);
QUDO_SLHDSA_keypair(ctx);
QUDO_SLHDSA_sign(ctx, sig, &sig_len, msg, msg_len);
QUDO_SLHDSA_verify(ctx, sig, sig_len, msg, msg_len);
QUDO_SLHDSA_free(ctx);
```

**Direct API:**
```c
QUDO_SLHDSA_SHA2_128s_keypair(pk, sk);
QUDO_SLHDSA_SHA2_128s_sign(sig, &sig_len, msg, msg_len, sk);
QUDO_SLHDSA_SHA2_128s_verify(sig, sig_len, msg, msg_len, pk);
```

## Documentation

- [Build and Install](../INSTALL.md) — full build instructions, all platforms
- [C API Reference](../docs/API.md) — complete API documentation
- [Algorithms](../docs/ALGORITHMS.md) — all algorithms and selection guide
- [Provider Configuration](../docs/PROVIDER.md) — OpenSSL EVP integration

## License

Apache License 2.0 and MIT License. See [LICENSE](LICENSE) for details.
