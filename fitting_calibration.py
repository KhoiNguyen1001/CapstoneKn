import numpy as np

R = np.array([3.989759931,3.499888148,2.999887,2.499838672,1.999684247,1.49971438,0.99977606])
M = np.array([2.037184792,1.787062417,1.531772688,1.276489907,1.021123964,0.765863334,0.510619499])

a, b = np.polyfit(R, M, 1)          # M = a*R + b
resid = M - (a*R + b)
print(f"gain a={a:.9f} (gain err {(a-1)*100:+.3f} %)  offset b={b:.4e} V")
print(f"residual std = {resid.std():.3e} V, max = {np.abs(resid).max():.3e} V")