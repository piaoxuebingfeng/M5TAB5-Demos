# 获取 bing 每日画报作为壁纸

  从 `http://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=zh-CN` 读取当日的壁纸 url，返回的 json 格式如下：

```
{
  "images": [
    {
      "startdate": "20250901",
      "fullstartdate": "202509011600",
      "enddate": "20250902",
      "url": "/th?id=OHR.DeadvleiTrees_ZH-CN0967414858_1920x1080.jpg&rf=LaDigue_1920x1080.jpg&pid=hp",
      "urlbase": "/th?id=OHR.DeadvleiTrees_ZH-CN0967414858",
      "copyright": "骆驼刺树, 死亡谷, 纳米布-诺克卢福国家公园, 纳米比亚 (© Inge Johnsson/Alamy Stock Photo)",
      "copyrightlink": "https://www.bing.com/search?q=%E7%BA%B3%E7%B1%B3%E6%AF%94%E4%BA%9A%E7%BA%B3%E7%B1%B3%E5%B8%83%E8%AF%BA%E5%85%8B%E8%B7%AF%E7%A6%8F%E5%85%AC%E5%9B%AD&form=hpcapt&mkt=zh-cn",
      "title": "死谷的幽灵",
      "quiz": "/search?q=Bing+homepage+quiz&filters=WQOskey:%22HPQuiz_20250901_DeadvleiTrees%22&FORM=HPQUIZ",
      "wp": true,
      "hsh": "5a90953aa8f36462fa4b1901ee601393",
      "drk": 1,
      "top": 1,
      "bot": 1,
      "hs": []
    }
  ],
  "tooltips": {
    "loading": "正在加载...",
    "previous": "上一个图像",
    "next": "下一个图像",
    "walle": "此图片不能下载用作壁纸。",
    "walls": "下载今日美图。仅限用作桌面壁纸。"
  }
}
```

  从返回的 json 中获取 url,在 `["images"][0]["url"]` 中。

  获取到的 url 为： `/th?id=OHR.DeadvleiTrees_ZH-CN0967414858_1920x1080.jpg&rf=LaDigue_1920x1080.jpg&pid=hp` 

  然后再将 url 和域名进行组合，得到完整的 url： `http://www.bing.com/th?id=OHR.DeadvleiTrees_ZH-CN0967414858_1920x1080.jpg&rf=LaDigue_1920x1080.jpg&pid=hp`

  将获取到的图片保存到 SPIFFS 中。

