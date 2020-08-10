# Test that timestamps can be updated.
# Fix #783: update query that modifies timestamp attribute fails.
CREATE TABLE xxx (c1 int, c2 timestamp)
INSERT INTO xxx (c1, c2) VALUES (1, '2020-01-02 12:23:34.56789')
INSERT INTO xxx (c1, c2) VALUES (2, '2020-01-02 11:22:33.721-05')
UPDATE xxx SET c2 = '2020-01-02 11:22:33.721-05' WHERE c1 = 2
SELECT * from xxx ORDER BY c1 ASC
DROP TABLE xxx