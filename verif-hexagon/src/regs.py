#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from src import log

m = ['m0', 'm1']

log.workaround("QTOOL-76764")
ctrls = [f'c{i}' for i in range(16) if i not in (4,8,11)]
dbl_ctrls = [f'c{i+1}:{i}' for i in range(0,16,2) if i not in (4,8,10)]

preds = [f'p{i}' for i in range(4)]
gprs = [f'r{i}' for i in range(29)]
gpr8 = [f'r{i}' for i in range(8)]
dpl_gprs = [f'r{i}' for i in list(range(8))+list(range(16,24))]
dbl_gprs = [f'r{i+1}:{i}' for i in range(0,28,2)]
dpl_dbl_gprs = [f'r{i+1}:{i}' for i in range(0,8,2)]
vec_preds4 = [f'q{i}' for i in range(4)]
vec_preds = [f'q{i}' for i in range(8)]
vec_dbl_preds = [f'q{i+1}:{i}' for i in range(0,4,2)]
vecs = [f'v{i}' for i in range(32)]
_inv_dbl_vecs = [f'v{i}:{i+1}' for i in range(0,29,2)]
log.workaround("QTOOL-100055")
#dbl_vecs = [f'v{i+1}:{i}' for i in range(0,29,2)] + _inv_dbl_vecs
dbl_vecs = [f'v{i+1}:{i}' for i in range(0,29,2)]
quad_vecs = [f'v{i+3}:{i}' for i in range(0,29,4)]
guest = [f'g{i}' for i in range(4)]
dbl_guest = [f'g{i+1}:{i}' for i in range(0,3,2)]
