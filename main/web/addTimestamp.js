// add-timestamp.js
import { readdirSync, statSync, readFileSync, writeFileSync } from "fs";
import { join } from "path";

const outputPath = "dist"; // 输出文件夹
const timestamp = new Date().getTime();

// 递归处理文件夹内的文件
function processFiles(folder) {
    const files = readdirSync(folder);
    for (const file of files) {
        const filePath = join(folder, file);
        const stats = statSync(filePath);
        if (stats.isFile()) {
            // 如果是文件，添加时间戳参数
            addTimestamp(filePath);
        } else if (stats.isDirectory()) {
            // 如果是文件夹，递归处理
            processFiles(filePath);
        }
    }
}

// 为文件添加时间戳参数
function addTimestamp(filePath) {
    if (filePath.endsWith(".html")) {
        // 只处理 HTML 文件
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

// 处理输出文件夹
processFiles(outputPath);
