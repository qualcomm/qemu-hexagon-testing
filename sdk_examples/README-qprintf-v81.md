<!--
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear
-->

# qprintf v81 SDK Reproducer

`package_qprintf_v81.sh` builds the Hexagon SDK 6.4.0.2 qprintf example and
packages the files required by QEMU's `tests/functional/hexagon/test_sdk.py`:

```sh
./sdk_examples/package_qprintf_v81.sh /opt/Hexagon_SDK/6.4.0.2
tar -xzf qemu-qurt-tests-sdk-v81.tar.gz
cd qemu-qurt-tests-bc94e62a20370dfe405220898fc2a64127fd64a6/sdk/V81QA_1
qemu-system-hexagon -M V81DGB_1 -m 4G -kernel ./runelf.pbn \
  -append './run_main_on_hexagon_sim -- ./libqprintf_example_q.so'
```

The package includes `runelf.pbn`, `run_main_on_hexagon_sim`, the test module,
and its `libqprintf.so` and `libworker_pool.so` dependencies.
