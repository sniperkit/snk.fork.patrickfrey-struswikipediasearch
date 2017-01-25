{% extends "search_resultlist_html.tpl" %}

{% block relatedblock %}
<p><b>LINE 4</b></p>
{% if relatedterms %}
<p><b>LINE 6</b></p>
<div id="relatedresult">
<p><b>LINE 8</b></p>
<ul>
{% for result in relatedterms %}
<p><b>LINE 11</b></p>
{% set queryparam = urlencode( result.value) %}
<li onclick="parent.location='evalQuery.php?q={{queryparam}}&s={{scheme}}'">
<div id="related">
<div id="related_term">{{ result.value }}</div>
<div id="related_weight">{{ "%.4f" % result.weight }}</div>
</div>
</li>
{% end %}
</ul>
</div>
{% end %}
{% end %}

{% block resultblock %}
<div id="searchresult">
<ul>
{% for result in results %}
{% set link = result.title.replace(' ','_') %}
<li onclick="parent.location='https://en.wikipedia.org/wiki/{{ link }}'">
<h3>{{ result.title }}</h3>
<div id="rank">
{% if mode == "debug" %}
<div id="rank_docno">{{ result.docno }}</div>
<div id="rank_weight">{{ "%.4f" % result.weight }}</div>
{% end %}
{% if len( result.paratitle ) > 0 %}
<div id="rank_paratitle"> {% raw result.paratitle %} </div><br/>
{% end %}
<div id="rank_abstract"> {% raw result.abstract %} </div>
</div>
</li>
{% end %}
</ul>
</div>
{% end %}

