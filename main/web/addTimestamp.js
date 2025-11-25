// add-timestamp.js
import { readdirSync, statSync, readFileSync, writeFileSync } from "fs";
import { join } from "path";

const outputPath = "dist"; // output folder
const timestamp = new Date().getTime();

// recursively process files in folder
function processFiles(folder) {
    const files = readdirSync(folder);
    for (const file of files) {
        const filePath = join(folder, file);
        const stats = statSync(filePath);
        if (stats.isFile()) {
            // if it's a file, add timestamp parameter
            addTimestamp(filePath);
        } else if (stats.isDirectory()) {
            // if it's a folder, process recursively
            processFiles(filePath);
        }
    }
}

// add timestamp parameter to file
function addTimestamp(filePath) {
    if (filePath.endsWith(".html")) {
        // only process HTML files
        const content = readFileSync(filePath, "utf-8");
        const updatedContent = content.replace(
            /(href=.*?\.css)|(src=.*?\.js)/g,
            (match) => {
                return `${match}?v=${timestamp}`;
            }
        );
        writeFileSync(filePath, updatedContent, "utf-8");
    }
}

// process output folder
processFiles(outputPath);
