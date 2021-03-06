import numpy as np
import matplotlib.pyplot as plt

x = [1,2,3,4,5,6,7,8,9,10]

y64p = np.array([13136926,13136932,13136932,13136924,13136930,
		13136930,13136933,13136939,13136932,13136932])
y64b = np.array([840763648,840763136,840763978,840763352,840763534,
		840763978,840763534,840764510,840763973,840763648])
y128p = np.array([7068454,7068448,7068460,7068448,7068451,
		7068443,7068438,7068458,7068443,7068451])
y128b = np.array([904762116,904760926,904762633,904761216,904761590,
		904760596,904759950,904762062,904761034,904761284])
y256p = np.array([4038007,4038006,4038022,4037988,4037997,
		4037999,4038016,4038000,4038001,4041924])
y256b = np.array([1033729172,1033728124,1033732874,1033724302,1033726922,
		1033728000,1033731594,1033728000,1033727940,1034731150])
y512p = np.array([2230287,2230291,2230284,2230279,2230285,
		2230282,2230282,2230282,2230286,2230290])
y512b = np.array([1141904150,1141905172,1141903756,1141902020,1141904960,
		1141903562,1141903562,1141904074,1141903960,1141905552])
y1024p = np.array([845390,845382,845352,845357,845381,
		845379,845349,845363,845368,845385])
y1024b = np.array([865674638,865661916,865633983,865644740,865662744,
		865665152,865635530,865646164,865651146,865669514])

y64p = y64p.sum() / 10000000.0
y64b = y64b.sum() / 10000000000.0
y128p = y128p.sum() / 10000000.0
y128b = y128b.sum() / 10000000000.0
y256p = y256p.sum() / 10000000.0
y256b = y256b.sum() / 10000000000.0
y512p = y512p.sum() / 10000000.0
y512b = y512b.sum() / 10000000000.0
y1024p = y1024p.sum() / 10000000.0
y1024b = y1024b.sum() / 10000000000.0

index = np.arange(5)
bar_width = 0.5

chart = plt.bar([0,1,2,3,4], [y64p,y128p,y256p,y512p,y1024p], bar_width)

#fig, ax = plt.subplots()
#rects = ax.bar([1,2,3,4,5], [y64p,y128p,y256p,y512p,y1024p], width)

plt.ylabel('packets per second (millions)')
plt.xlabel('packet size (bytes)')
plt.xticks(index + (bar_width/2), ["64", "128", "256", "512", "1024"])
plt.yticks(np.arange(0, 15, 1))

plt.show()

# print(y64b)
# print(y128b)
# print(y256b)
# print(y512b)
# print(y1024b)

# plt.plot(x, y64p, label="64 byte") 
# plt.plot(x, y128p, label="128 byte")
# plt.plot(x, y256p, label="256 byte")
# plt.plot(x, y512p, label="512 byte")
# plt.plot(x, y1024p, label="1024 byte")

# plt.xlabel('Seconds')
# plt.ylabel('Number of received packets (millions)')

# plt.xlim(1, 10)
# plt.ylim(0, 14)
# plt.legend()

# plt.plot(x, y64b, label="64 byte") 
# plt.plot(x, y128b, label="128 byte")
# plt.plot(x, y256b, label="256 byte")
# plt.plot(x, y512b, label="512 byte")
# plt.plot(x, y1024b, label="1024 byte")

# plt.xlabel('Seconds')
# plt.ylabel('Number of received bits (Giga)')

# plt.xlim(1, 10)
# plt.ylim(0, 2000)
# plt.legend()

# fig, ax1 = plt.subplots()
# ax1.plot([y64b, y128b, y256b, y512b, y1024b])
# ax1.set_xlabel('dsf')
# ax1.set_ylabel('asd')

# ax2 = ax1.twinx()
# ax2.plot([y64p, y128p, y256p, y512p, y1024p])
# ax2.set_ylabel('asfsdd')


# #plt.tight_layout()
# plt.show()
