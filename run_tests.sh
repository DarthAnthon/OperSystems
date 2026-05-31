#!/bin/bash

echo "=== PREPARING TEST DATA ==="
mkdir -p test_data output
mkdir -p test_data/subdir1/subdir2/subdir3/subdir4

echo "Hello, World!" > test_data/file1.txt
echo "This is file 2" > test_data/file2.txt
echo "Content of file in subdir1" > test_data/subdir1/file3.txt
echo "Content of file in subdir2" > test_data/subdir1/subdir2/file4.txt
echo "Content of file in subdir3" > test_data/subdir1/subdir2/subdir3/file5.txt
echo "Content of file in subdir4 (deep nesting)" > test_data/subdir1/subdir2/subdir3/subdir4/file6.txt

dd if=/dev/urandom of=test_data/binary.bin bs=1024 count=10 2>/dev/null
touch test_data/empty.txt
dd if=/dev/zero of=test_data/large.bin bs=1M count=1 2>/dev/null

mkdir -p "test_data/special_dir"
echo "Special content" > "test_data/special_dir/file_with-dash.txt"

#rm -f disk.img

echo ""
echo "=== TEST 1: Create new image ==="
LD_LIBRARY_PATH=. ./secure_copy -add -key "secret" -image disk.img test_data/file1.txt

echo ""
echo "=== TEST 2: Add multiple files ==="
LD_LIBRARY_PATH=. ./secure_copy -add -key "secret" -image disk.img test_data/file2.txt

echo ""
echo "=== TEST 3: Add directory ==="
LD_LIBRARY_PATH=. ./secure_copy -add -key "secret" -image disk.img test_data/subdir1/

echo ""
echo "=== TEST 4: Add nested directory ==="
LD_LIBRARY_PATH=. ./secure_copy -add -key "secret" -image disk.img test_data/special_dir/

echo ""
echo "=== TEST 4.5: Add binary, empty and large files ==="
LD_LIBRARY_PATH=. ./secure_copy -add -key "secret" -image disk.img test_data/binary.bin test_data/empty.txt test_data/large.bin

echo ""
echo "=== TEST 5: List files ==="
LD_LIBRARY_PATH=. ./secure_copy -list -image disk.img

echo ""
echo "=== TEST 6: Extract file (using full path from image) ==="
LD_LIBRARY_PATH=. ./secure_copy -get -image disk.img -key "secret" -out output/file1.txt test_data/file1.txt
echo "Comparing..."
cmp test_data/file1.txt output/file1.txt && echo "✓ Files match" || echo "✗ Files differ"

echo ""
echo "=== TEST 7: Extract nested file (using correct path from listing) ==="
LD_LIBRARY_PATH=. ./secure_copy -get -image disk.img -key "secret" -out output/file6.txt subdir2/subdir3/subdir4/file6.txt
cmp test_data/subdir1/subdir2/subdir3/subdir4/file6.txt output/file6.txt && echo "✓ Files match" || echo "✗ Files differ"

echo ""
echo "=== TEST 8: Extract special_dir file ==="
LD_LIBRARY_PATH=. ./secure_copy -get -image disk.img -key "secret" -out output/special.txt file_with-dash.txt
cmp "test_data/special_dir/file_with-dash.txt" output/special.txt && echo "✓ Files match" || echo "✗ Files differ"

echo ""
echo "=== TEST 9: Extract with wrong key ==="
LD_LIBRARY_PATH=. ./secure_copy -get -image disk.img -key "wrongkey" -out output/wrong.txt test_data/file1.txt 2>/dev/null
cmp -s test_data/file1.txt output/wrong.txt && echo "✗ Should differ" || echo "✓ Content differs (as expected)"

echo ""
echo "=== TEST 10: Non-existent file ==="
LD_LIBRARY_PATH=. ./secure_copy -get -image disk.img -key "secret" -out output/dummy.txt nonexistent.txt 2>&1 | grep -q "ERROR" && echo "✓ Error reported" || echo "✗ No error"

echo ""
echo "=== TEST 11: Non-existent image ==="
LD_LIBRARY_PATH=. ./secure_copy -list -image nonexistent.img 2>&1 | grep -q "ERROR" && echo "✓ Error reported" || echo "✗ No error"

echo ""
echo "=== TEST 12: Extract binary file ==="
LD_LIBRARY_PATH=. ./secure_copy -get -image disk.img -key "secret" -out output/binary.bin test_data/binary.bin
cmp test_data/binary.bin output/binary.bin && echo "✓ Binary files match" || echo "✗ Binary files differ"

echo ""
echo "=== TEST 13: Extract empty file ==="
LD_LIBRARY_PATH=. ./secure_copy -get -image disk.img -key "secret" -out output/empty.txt test_data/empty.txt
cmp test_data/empty.txt output/empty.txt && echo "✓ Empty file OK" || echo "✗ Empty file differs"

echo ""
echo "=== TEST 14: Extract large file ==="
LD_LIBRARY_PATH=. ./secure_copy -get -image disk.img -key "secret" -out output/large.bin test_data/large.bin
cmp test_data/large.bin output/large.bin && echo "✓ Large file match" || echo "✗ Large file differs"

echo ""
echo "=== TEST 15: Add more files to existing image ==="
echo "New file content" > test_data/newfile.txt
LD_LIBRARY_PATH=. ./secure_copy -add -key "secret" -image disk.img test_data/newfile.txt
echo "Verifying newfile is in image..."
LD_LIBRARY_PATH=. ./secure_copy -list -image disk.img | grep -q "test_data/newfile.txt" && echo "✓ Newfile added" || echo "✗ Newfile not found"

echo ""
echo "=== TEST 16: Verify file count after multiple additions ==="
count=$(LD_LIBRARY_PATH=. ./secure_copy -list -image disk.img 2>&1 | head -1 | grep -oE "[0-9]+")
echo "Total files in image: $count"
[ "$count" -ge 10 ] && echo "✓ Multiple files stored" || echo "✗ Unexpected file count"

echo ""
echo "=== CLEANUP ==="
# rm -rf test_data output disk.img

echo ""
echo "╔════════════════════════════════════════╗"
echo "║  ✓✓✓ ALL TESTS COMPLETED ✓✓✓         ║"
echo "╚════════════════════════════════════════╝"