import re

with open('src/CrossPointSettings.cpp', 'r') as f:
    code = f.read()

write_block = re.search(r'uint8_t CrossPointSettings::writeSettings.*?return writer.item_count;', code, re.DOTALL).group(0)
writes = re.findall(r'writer\.writeItem(?:String)?\(\s*file\s*,\s*([a-zA-Z0-9_]+)\s*\)', write_block)

load_block = re.search(r'bool CrossPointSettings::loadFromFile\(\).*?return true;\n}', code, re.DOTALL).group(0)
reads = re.findall(r'(?:readAndValidate|serialization::readPod|serialization::readString)\(\s*inputFile\s*,\s*([a-zA-Z0-9_]+)', load_block)

print(f"Writes count: {len(writes)}")
print(f"Reads count: {len(reads)}")

for i, (w, r) in enumerate(zip(writes, reads)):
    if w != r:
        print(f"Mismatch at index {i}: write={w}, read={r}")

if len(writes) > len(reads):
    print("Extra writes:", writes[len(reads):])
elif len(reads) > len(writes):
    print("Extra reads:", reads[len(writes):])
