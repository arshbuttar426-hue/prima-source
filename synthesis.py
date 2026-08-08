def decide(thesis, antithesis):
    consensus = (thesis + antithesis) / 2
    richness = abs(thesis - antithesis)
    return consensus, richness

print(decide(0.8, 0.3))
print(decide(0.9, 0.85))
